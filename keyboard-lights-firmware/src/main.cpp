#include <Arduino.h>
#include <FastLED.h>

#include "app_conf.h"
#include "hardware.h"
#include "midi_util.h"
#include "light_modes.h"
#include "chord_modes.h"
#include "key_colors.h"
#include "serial_commands.h"
#include "rotary_encoder.h"

namespace kbl {
    namespace keys {
        static const uint8_t MIDI_NOTE_LOWEST = 21; // A0, maps to LED 0
        static const uint8_t MIDI_NOTE_HIGHEST = MIDI_NOTE_LOWEST + leds::NUM_LEDS - 1; // C8
        static const uint8_t MIDI_CC_SUSTAIN = 64;
        static const uint8_t MIDI_CC_ALL_SOUND_OFF = 120;
        static const uint8_t MIDI_CC_ALL_NOTES_OFF = 123;
    }
    namespace lights {
        static const uint32_t FRAME_MS = 10; // ~100 fps; fade speed depends on this
        static const uint8_t FADE_PER_FRAME = 40; // out of 255, applied to released keys
        static const uint8_t BASE_HUE = HUE_AQUA;
        static const uint8_t MIN_BRIGHTNESS = 32; // so the softest notes are still visible
        static const uint8_t MASTER_BRIGHTNESS_STEPS = 16; // lowest step keeps the softest notes visible
        static const uint32_t MODE_PREVIEW_MS = 500;
        static const uint32_t BRIGHTNESS_BAR_MS = 1000;
    }
    namespace demo {
        static const uint32_t IDLE_MS = 5 * 60 * 1000; // starts by itself after this long without playing
        static const uint32_t ROLL_MS = 200; // 5 Hz
        static const uint16_t ROLL_MAX = 500; // a roll below NUM_LEDS lights that key, so higher = sparser
        static const uint32_t KEY_MS = 1000;
    }
}

CRGB kblLEDs[kbl::leds::NUM_LEDS];

struct KeyState {
    bool held;       // key is physically down
    bool sustained;  // key was released while the sustain pedal was down
    uint8_t velocity;
};

KeyState keyStates[kbl::leds::NUM_LEDS];

// Simulated keys for Demo Mode, kept apart from keyStates so they can't trigger Mode Select.
KeyState demoKeys[kbl::leds::NUM_LEDS];
uint32_t demoKeyStartMs[kbl::leds::NUM_LEDS];
bool demoMode = false;
uint32_t lastActivityMs = 0; // last key or sustain pedal event

// Per-LED brightness scale (255 = unscaled) compensating for supply sag along the strip.
// Assumes the drop grows linearly from LED 0 (supply end) to FAR_END_VOLTAGE at the last LED,
// and that each LED's power, and so its brightness, is proportional to its supply voltage.
// Brighter near-end LEDs are scaled down to match the dimmest (far) one. Strip values are
// linear PWM duty, so the scale is applied directly to the final colors with no gamma curve.
uint8_t sagCompensation[kbl::leds::NUM_LEDS];

void buildSagCompensation() {
    const float sag = kbl::leds::PSU_VOLTAGE - kbl::leds::FAR_END_VOLTAGE;
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++) {
        float ledVoltage = kbl::leds::PSU_VOLTAGE - sag * i / (kbl::leds::NUM_LEDS - 1);
        float powerLost = 1.0f - ledVoltage / kbl::leds::PSU_VOLTAGE; // fraction lost to resistance
        float farEndPowerLost = sag / kbl::leds::PSU_VOLTAGE;
        sagCompensation[i] = lroundf(255.0f * (1.0f - farEndPowerLost) / (1.0f - powerLost));
    }
    debugf("Sag compensation: LED 0 = %d, LED %d = %d\n",
        sagCompensation[0], (int)kbl::leds::NUM_LEDS - 1, sagCompensation[kbl::leds::NUM_LEDS - 1]);
}
bool sustainPedalDown = false;

// armed when MODE_SELECT_KEY goes down with no other key held
bool modeSelectArmed = false;
uint32_t modeSelectKeyDownMs = 0;

LightMode lightMode = LightMode::PitchHue;
uint8_t masterBrightness = kbl::lights::MASTER_BRIGHTNESS_STEPS;

// While a preview runs it takes over every LED; key state keeps updating underneath.
CRGB previewLEDs[kbl::leds::NUM_LEDS];
uint32_t previewStartMs = 0;
uint32_t previewDurationMs = 0; // 0 = no preview running

bool isSounding(const KeyState& key) {
    return key.held || key.sustained;
}

uint8_t velocityBrightness(uint8_t velocity) {
    return map(velocity, 1, 127, kbl::lights::MIN_BRIGHTNESS, 255);
}

// PitchClass of a key, 0 = C ... 11 = B
uint8_t keyPitchClass(uint8_t keyIndex) {
    return (keyIndex + kbl::keys::MIDI_NOTE_LOWEST) % 12;
}

// Per-key modes: color of a Sounding key, from its own state only.
CRGB keyColor(uint8_t keyIndex, const KeyState& key) {
    switch (lightMode) {
        case LightMode::FullBrightness:
            return CHSV(kbl::lights::BASE_HUE, 255, 255);
        case LightMode::PitchHue:
            return CHSV(keyIndex * 255 / (kbl::leds::NUM_LEDS - 1), 255, velocityBrightness(key.velocity));
        case LightMode::KeyColors:
            return CRGB(kbl::keycolors::PITCH_CLASS_COLORS[keyPitchClass(keyIndex)]).nscale8(velocityBrightness(key.velocity));
        case LightMode::VelocityBrightness:
        default:
            return CHSV(kbl::lights::BASE_HUE, 255, velocityBrightness(key.velocity));
    }
}

// Each Sounding PitchClass lights all of its keys, at the hardest-pressed one's brightness.
void renderAllOctaves(CRGB* target, const KeyState* keys) {
    uint8_t maxVelocity[12] = {};
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++) {
        if (isSounding(keys[i])) {
            uint8_t& v = maxVelocity[keyPitchClass(i)];
            v = max(v, keys[i].velocity);
        }
    }
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++) {
        uint8_t pc = keyPitchClass(i);
        target[i] = maxVelocity[pc]
            ? CRGB(kbl::keycolors::PITCH_CLASS_COLORS[pc]).nscale8(velocityBrightness(maxVelocity[pc]))
            : CRGB::Black;
    }
}

// Fills target with what each LED should show, from the state of all keys.
// Black = not lit (the LED fades out). Modes that light keys other than the pressed one
// get their own render function here; everything else is a per-key mode via keyColor().
void computeTargets(CRGB* target, const KeyState* keys) {
    switch (lightMode) {
        case LightMode::AllOctaves:
            renderAllOctaves(target, keys);
            break;
        default:
            for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++) {
                target[i] = isSounding(keys[i]) ? keyColor(i, keys[i]) : CRGB::Black;
            }
            break;
    }
}

// previewLEDs must already be filled
void startPreview(uint32_t durationMs) {
    previewStartMs = millis();
    previewDurationMs = durationMs;
}

bool previewRunning() {
    if (previewDurationMs && millis() - previewStartMs >= previewDurationMs) {
        previewDurationMs = 0;
    }
    return previewDurationMs != 0;
}

// Renders the current mode from simulated keys, so the preview always matches the mode.
void showModePreview() {
    KeyState keys[kbl::leds::NUM_LEDS] = {};
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++) {
        keys[i].held = true;
        keys[i].velocity = 127;
        switch (lightMode) {
            case LightMode::VelocityBrightness:
                keys[i].velocity = map(i, 0, kbl::leds::NUM_LEDS - 1, 1, 127);
                break;
            case LightMode::AllOctaves: {
                using namespace kbl::chords;
                keys[i].held = chord({C, E, G}) & (1u << keyPitchClass(i));
                break;
            }
            default:
                break;
        }
    }
    computeTargets(previewLEDs, keys);
    startPreview(kbl::lights::MODE_PREVIEW_MS);
}

void setLightMode(LightMode mode) {
    lightMode = mode;
    debugf("Light mode: %s\n", lightModeName(mode));
    showModePreview();
}

void showBrightnessBar() {
    uint8_t litLEDs = masterBrightness * kbl::leds::NUM_LEDS / kbl::lights::MASTER_BRIGHTNESS_STEPS;
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++) {
        previewLEDs[i] = i < litLEDs ? CRGB(CHSV(kbl::lights::BASE_HUE, 255, 255)) : CRGB(CRGB::Black);
    }
    startPreview(kbl::lights::BRIGHTNESS_BAR_MS);
}

void setMasterBrightness(int level) {
    masterBrightness = constrain(level, 1, kbl::lights::MASTER_BRIGHTNESS_STEPS);
    FastLED.setBrightness(masterBrightness * 255 / kbl::lights::MASTER_BRIGHTNESS_STEPS);
    debugf("Master brightness: %d\n", masterBrightness);
    showBrightnessBar();
}

void clearKeys() {
    for (KeyState& key : keyStates) {
        key = {};
    }
    modeSelectArmed = false;
}

// also clears stuck keys, for when the piano never sends a note-off
void enterDemoMode() {
    if (demoMode) return;
    clearKeys();
    sustainPedalDown = false;
    demoMode = true;
    debugf("Demo mode: on\n");
}

void exitDemoMode() {
    if (!demoMode) return;
    demoMode = false;
    for (KeyState& key : demoKeys) {
        key = {};
    }
    debugf("Demo mode: off\n");
}

void updateDemoKeys() {
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++) {
        if (demoKeys[i].held && millis() - demoKeyStartMs[i] >= kbl::demo::KEY_MS) {
            demoKeys[i].held = false;
        }
    }
    EVERY_N_MILLISECONDS(kbl::demo::ROLL_MS) {
        uint32_t roll = random(kbl::demo::ROLL_MAX + 1);
        if (roll < kbl::leds::NUM_LEDS) {
            demoKeys[roll].held = true;
            demoKeys[roll].velocity = random(1, 128);
            demoKeyStartMs[roll] = millis();
        }
    }
}

// returns -1 for notes outside the 88-key range
int noteToKeyIndex(byte note) {
    if (note < kbl::keys::MIDI_NOTE_LOWEST || note > kbl::keys::MIDI_NOTE_HIGHEST) {
        return -1;
    }
    return note - kbl::keys::MIDI_NOTE_LOWEST;
}

// bitmask of held pitch classes (bit 0 = C), ignoring the mode select key
uint16_t heldChord() {
    uint16_t mask = 0;
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++) {
        if (keyStates[i].held && i != kbl::chords::MODE_SELECT_KEY) {
            mask |= 1u << keyPitchClass(i);
        }
    }
    return mask;
}

void checkModeChord() {
    uint16_t held = heldChord();
    for (const kbl::chords::ChordMode& cm : kbl::chords::CHORD_MODES) {
        if (cm.chord == held) {
            setLightMode(cm.mode);
            return;
        }
    }
}

void handleNoteOn(byte channel, byte note, byte velocity) {
    int i = noteToKeyIndex(note);
    if (i < 0) return;

    lastActivityMs = millis();
    exitDemoMode();

    if (i == kbl::chords::MODE_SELECT_KEY) {
        modeSelectArmed = heldChord() == 0;
        modeSelectKeyDownMs = millis();
    }

    keyStates[i].held = true;
    keyStates[i].sustained = false;
    keyStates[i].velocity = velocity;

    if (modeSelectArmed && i != kbl::chords::MODE_SELECT_KEY
        && millis() - modeSelectKeyDownMs >= kbl::chords::MODE_SELECT_HOLD_MS) {
        checkModeChord();
    }
}

// the MIDI library also routes note-on with velocity 0 here
void handleNoteOff(byte channel, byte note, byte velocity) {
    int i = noteToKeyIndex(note);
    if (i < 0) return;

    lastActivityMs = millis();

    if (i == kbl::chords::MODE_SELECT_KEY) {
        modeSelectArmed = false;
    }

    keyStates[i].held = false;
    keyStates[i].sustained = sustainPedalDown;
}

void handleControlChange(byte channel, byte number, byte value) {
    if (number == kbl::keys::MIDI_CC_ALL_SOUND_OFF || number == kbl::keys::MIDI_CC_ALL_NOTES_OFF) {
        clearKeys();
        return;
    }
    if (number != kbl::keys::MIDI_CC_SUSTAIN) return;

    lastActivityMs = millis();

    sustainPedalDown = value >= 64;
    if (sustainPedalDown) {
        exitDemoMode();
    } else {
        for (KeyState& key : keyStates) {
            key.sustained = false;
        }
    }
}

void onRotaryTurn(int steps) {
    setMasterBrightness(masterBrightness + steps);
}

void onRotaryPress() {
    enterDemoMode();
}

void renderKeys() {
    CRGB frame[kbl::leds::NUM_LEDS];
    const CRGB* target = frame;
    if (previewRunning()) {
        target = previewLEDs;
    } else {
        computeTargets(frame, demoMode ? demoKeys : keyStates);
    }

    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++) {
        if (target[i]) {
            kblLEDs[i] = target[i];
            kblLEDs[i].nscale8(sagCompensation[i]);
        } else {
            kblLEDs[i].fadeToBlackBy(kbl::lights::FADE_PER_FRAME);
        }
    }
}

void setup() {

  debugStart();
  debugWaitForInterface();

  debugf("Serial Initialized");

  // connected to high side of optocoupler w/ external pullup.
  // todo: does library configure?
  pinMode(kbl::pins::MIDI_RX, INPUT);

  rotaryBegin();

  pinMode(kbl::pins::ENABLE_PSU, OUTPUT);
  // pinMode(kbl::pins::RGB_DATA, OUTPUT); // configured by fastled
 
  digitalWrite(kbl::pins::ENABLE_PSU, HIGH);

  debugf("Pinout Initialized");


  FastLED.addLeds<WS2812B, kbl::pins::RGB_DATA, EOrder::GRB>(kblLEDs, kbl::leds::NUM_LEDS);
  FastLED.setMaxPowerInMilliWatts(kbl::leds::PSU_MAX_MA * kbl::leds::PSU_VOLTAGE);
  buildSagCompensation();

  showModePreview();

  debugf("LEDS Initialized");

  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandleControlChange(handleControlChange);
  MIDI.begin(MIDI_CHANNEL_OMNI);

  debugf("MIDI Initialized");

  serialCommandsBegin();
}

void loop() {

  // drain everything buffered so held/released state is current before rendering
  while (MIDI.read()) {}
  serialCommandsPoll(); // no-op unless KBL_USB_MIDI_DEBUG
  rotaryPoll();

  if (!demoMode && millis() - lastActivityMs >= kbl::demo::IDLE_MS) {
    enterDemoMode();
  }
  if (demoMode) {
    updateDemoKeys();
  }

  EVERY_N_MILLISECONDS(kbl::lights::FRAME_MS) {
    renderKeys();
    FastLED.show();
  }

  EVERY_N_SECONDS(2) {
    debugf("FastLED at %d fps\n", FastLED.getFPS());
  }

}

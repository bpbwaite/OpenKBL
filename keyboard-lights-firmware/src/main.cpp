#include <Arduino.h>
#include <FastLED.h>

#include "app_conf.h"
#include "hardware.h"
#include "midi_util.h"
#include "light_modes.h"
#include "chord_modes.h"
#include "key_state.h"
#include "demo_mode.h"
#include "serial_commands.h"
#include "rotary_encoder.h"

CRGB kblLEDs[kbl::leds::NUM_LEDS];

KeyState keyStates[kbl::leds::NUM_LEDS];

// Per-LED brightness scale (255 = unscaled) compensating for supply sag along the strip.
// Assumes the drop grows linearly from LED 0 (supply end) to FAR_END_VOLTAGE at the last LED,
// and that each LED's power, and so its brightness, is proportional to its supply voltage.
// Brighter near-end LEDs are scaled down to match the dimmest (far) one. Strip values are
// linear PWM duty, so the scale is applied directly to the final colors with no gamma curve.
uint8_t sagCompensation[kbl::leds::NUM_LEDS];

void buildSagCompensation()
{
    const float sag = kbl::leds::PSU_VOLTAGE - kbl::leds::FAR_END_VOLTAGE;
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++)
    {
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

LightMode lightMode = kbl::lights::DEFAULT_LIGHT_MODE;
uint8_t masterBrightness = kbl::lights::MASTER_BRIGHTNESS_STEPS;
uint8_t masterScale = 255; // FastLED global brightness for masterBrightness, before power limiting
uint32_t powerBudgetMW = kbl::leds::PSU_MAX_MA * kbl::leds::PSU_VOLTAGE;

// While a preview runs it takes over every LED; key state keeps updating underneath.
CRGB previewLEDs[kbl::leds::NUM_LEDS];
uint32_t previewStartMs = 0;
uint32_t previewDurationMs = 0; // 0 = no preview running

uint8_t velocityBrightness(uint8_t velocity)
{
    return map(velocity, 1, 127, kbl::leds::MIN_PEAK, 255);
}

// PitchClass of a key, 0 = C ... 11 = B
uint8_t keyPitchClass(uint8_t keyIndex)
{
    return (keyIndex + kbl::keys::MIDI_NOTE_LOWEST) % 12;
}

// Per-key modes: color of a Sounding key, from its own state only.
CRGB keyColor(uint8_t keyIndex, const KeyState &key)
{
    switch (lightMode)
    {
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
void renderAllOctaves(CRGB *target, const KeyState *keys)
{
    uint8_t maxVelocity[12] = {};
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++)
    {
        if (isSounding(keys[i]))
        {
            uint8_t &v = maxVelocity[keyPitchClass(i)];
            v = max(v, keys[i].velocity);
        }
    }
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++)
    {
        uint8_t pc = keyPitchClass(i);
        target[i] = maxVelocity[pc]
                        ? CRGB(kbl::keycolors::PITCH_CLASS_COLORS[pc]).nscale8(velocityBrightness(maxVelocity[pc]))
                        : CRGB::Black;
    }
}

// Fills target with what each LED should show, from the state of all keys.
// Black = not lit (the LED fades out). Modes that light keys other than the pressed one
// get their own render function here; everything else is a per-key mode via keyColor().
void computeTargets(CRGB *target, const KeyState *keys)
{
    switch (lightMode)
    {
    case LightMode::AllOctaves:
        renderAllOctaves(target, keys);
        break;
    default:
        for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++)
        {
            target[i] = isSounding(keys[i]) ? keyColor(i, keys[i]) : CRGB::Black;
        }
        break;
    }
}

// previewLEDs must already be filled
void startPreview(uint32_t durationMs)
{
    previewStartMs = millis();
    previewDurationMs = durationMs;
}

bool previewRunning()
{
    if (previewDurationMs && millis() - previewStartMs >= previewDurationMs)
    {
        previewDurationMs = 0;
    }
    return previewDurationMs != 0;
}

// Renders the current mode from simulated keys, so the preview always matches the mode.
void showModePreview()
{
    KeyState keys[kbl::leds::NUM_LEDS] = {};
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++)
    {
        keys[i].held = true;
        keys[i].velocity = 127;
        switch (lightMode)
        {
        case LightMode::VelocityBrightness:
            keys[i].velocity = map(i, 0, kbl::leds::NUM_LEDS - 1, 1, 127);
            break;
        case LightMode::AllOctaves:
        {
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

void setLightMode(LightMode mode)
{
    lightMode = mode;
    debugf("Light mode: %s\n", lightModeName(mode));
    showModePreview();
}

void showBrightnessBar()
{
    uint8_t litLEDs = masterBrightness * kbl::leds::NUM_LEDS / kbl::lights::MASTER_BRIGHTNESS_STEPS;
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++)
    {
        previewLEDs[i] = i < litLEDs ? CRGB(CHSV(kbl::lights::BASE_HUE, 255, 255)) : CRGB(CRGB::Black);
    }
    startPreview(kbl::lights::BRIGHTNESS_BAR_MS);
}

void setMasterBrightness(int level)
{
    masterBrightness = constrain(level, 1, kbl::lights::MASTER_BRIGHTNESS_STEPS);
    masterScale = masterBrightness * 255 / kbl::lights::MASTER_BRIGHTNESS_STEPS;
    debugf("Master brightness: %d\n", masterBrightness);
    showBrightnessBar();
}

void clearKeys()
{
    for (KeyState &key : keyStates)
    {
        key = {};
    }
    modeSelectArmed = false;
}

void onDemoModeStart()
{
    clearKeys();
    sustainPedalDown = false;
}

// returns -1 for notes outside the 88-key range
int noteToKeyIndex(byte note)
{
    if (note < kbl::keys::MIDI_NOTE_LOWEST || note > kbl::keys::MIDI_NOTE_HIGHEST)
    {
        return -1;
    }
    return note - kbl::keys::MIDI_NOTE_LOWEST;
}

// bitmask of held pitch classes (bit 0 = C), ignoring the mode select key
uint16_t heldChord()
{
    uint16_t mask = 0;
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++)
    {
        if (keyStates[i].held && i != kbl::chords::MODE_SELECT_KEY)
        {
            mask |= 1u << keyPitchClass(i);
        }
    }
    return mask;
}

void checkModeChord()
{
    uint16_t held = heldChord();
    for (const kbl::chords::ChordMode &cm : kbl::chords::CHORD_MODES)
    {
        if (cm.chord == held)
        {
            setLightMode(cm.mode);
            return;
        }
    }
}

void handleNoteOn(byte channel, byte note, byte velocity)
{
    int i = noteToKeyIndex(note);
    if (i < 0)
        return;

#ifdef KBL_USB_MIDI_DEBUG
    debugf("Note: %d\n", note);
#endif
    kbl::demo::recordActivity();
    kbl::demo::stop();

    if (i == kbl::chords::MODE_SELECT_KEY)
    {
        modeSelectArmed = heldChord() == 0;
        modeSelectKeyDownMs = millis();
    }

    keyStates[i].held = true;
    keyStates[i].sustained = false;
    keyStates[i].velocity = velocity;

    if (modeSelectArmed && i != kbl::chords::MODE_SELECT_KEY && millis() - modeSelectKeyDownMs >= kbl::chords::MODE_SELECT_HOLD_MS)
    {
        checkModeChord();
    }
}

// the MIDI library also routes note-on with velocity 0 here
void handleNoteOff(byte channel, byte note, byte velocity)
{
    int i = noteToKeyIndex(note);
    if (i < 0)
        return;

    kbl::demo::recordActivity();

    if (i == kbl::chords::MODE_SELECT_KEY)
    {
        modeSelectArmed = false;
    }

    keyStates[i].held = false;
    keyStates[i].sustained = sustainPedalDown;
}

void handleControlChange(byte channel, byte number, byte value)
{
    if (number == kbl::midi::CC_ALL_SOUND_OFF || number == kbl::midi::CC_ALL_NOTES_OFF)
    {
        clearKeys();
        return;
    }
    if (number != kbl::midi::CC_SUSTAIN)
        return;

    kbl::demo::recordActivity();

    sustainPedalDown = value >= 64;
    if (sustainPedalDown)
    {
        kbl::demo::stop();
    }
    else
    {
        for (KeyState &key : keyStates)
        {
            key.sustained = false;
        }
    }
}

void onRotaryTurn(int steps)
{
    setMasterBrightness(masterBrightness + steps);
}

void onRotaryPress()
{
    kbl::demo::start();
}

// Smallest buffer value that still reaches the strip as at least MIN_PEAK under global
// brightness `scale`. FastLED sends (value * (scale + 1)) >> 8; dithering only adds to that.
uint8_t floorBeforeScale(uint8_t scale)
{
    uint16_t value = ((uint16_t)kbl::leds::MIN_PEAK * 256 + scale) / (scale + 1); // rounded up
    return min<uint16_t>(value, 255);
}

// Keeps every LED out of the flickering range: lit LEDs below the floor are raised to it,
// keeping their color, and fading LEDs that drop below it go dark.
void applyBrightnessFloor(const CRGB *target, uint8_t scale)
{
    const uint8_t floorPeak = floorBeforeScale(scale);
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++)
    {
        CRGB &led = kblLEDs[i];
        uint8_t peak = max(led.r, max(led.g, led.b));
        if (peak == 0 || peak >= floorPeak)
            continue;
        if (target[i])
        {
            led = CRGB((led.r * floorPeak + peak / 2) / peak,
                       (led.g * floorPeak + peak / 2) / peak,
                       (led.b * floorPeak + peak / 2) / peak);
        }
        else
        {
            led = CRGB::Black;
        }
    }
}

// Fills kblLEDs and returns the global brightness to show them with.
uint8_t renderKeys()
{
    CRGB frame[kbl::leds::NUM_LEDS];
    const CRGB *target = frame;
    if (previewRunning())
    {
        target = previewLEDs;
    }
    else
    {
        computeTargets(frame, kbl::demo::active() ? kbl::demo::keys() : keyStates);
    }

    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++)
    {
        if (target[i])
        {
            kblLEDs[i] = target[i];
            kblLEDs[i].nscale8(sagCompensation[i]);
        }
        else
        {
            kblLEDs[i].fadeToBlackBy(kbl::lights::FADE_PER_FRAME);
        }
    }

    // Power limiting is done here rather than by FastLED, so the floor can allow for it.
    uint8_t scale = calculate_max_brightness_for_power_mW(kblLEDs, kbl::leds::NUM_LEDS, masterScale, powerBudgetMW);
    applyBrightnessFloor(target, scale);
    // raising dim LEDs adds a little power; settle once more
    uint8_t settled = calculate_max_brightness_for_power_mW(kblLEDs, kbl::leds::NUM_LEDS, scale, powerBudgetMW);
    if (settled < scale)
    {
        scale = settled;
        applyBrightnessFloor(target, scale);
    }
    return scale;
}

void setup()
{

    debugStart();
    debugWaitForInterface();

    debugf("Serial Initialized");

    // connected to high side of optocoupler w/ external pullup.
    // todo: does library configure?
    pinMode(kbl::pins::MIDI_RX, INPUT);

    rotaryBegin();

    // pinMode(kbl::pins::RGB_DATA, OUTPUT); // configured by fastled

    // pinMode(kbl::pins::ENABLE_PSU, OUTPUT); // shorted high on first revision, leave like this
    digitalWrite(kbl::pins::ENABLE_PSU, HIGH);

    debugf("Pinout Initialized");

    FastLED.addLeds<WS2812B, kbl::pins::RGB_DATA, EOrder::GRB>(kblLEDs, kbl::leds::NUM_LEDS);

    if (PCB_REVISION == 0)
    {
        // the board may or may not be running on USB, so we should limit current to around 500mA
        // I'll uncomment this line when I deploy the board
        // Alternative smart thing to do: assume the PSU is connected, but if we detect a PC connection at any point during operation,
        // limit draw to 500mA.
        powerBudgetMW = kbl::leds::USB_MAX_MA * kbl::leds::PSU_VOLTAGE;
    }

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

void loop()
{

    // drain everything buffered so held/released state is current before rendering
    while (MIDI.read())
    {
    }
    serialCommandsPoll(); // no-op unless KBL_USB_MIDI_DEBUG
    rotaryPoll();

    kbl::demo::poll();

    EVERY_N_MILLISECONDS(kbl::lights::FRAME_MS)
    {
        FastLED.show(renderKeys()); // dithering stays on (FastLED's default)
    }

    EVERY_N_SECONDS(kbl::debug::FPS_REPORT_S)
    {
        debugf("FastLED at %d fps\n", FastLED.getFPS());
    }
}

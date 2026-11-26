#include <Arduino.h>
#include <FastLED.h>

#include "app_conf.h"
#include "hardware.h"
#include "midi_util.h"
#include "key_state.h"
#include "render.h"
#include "settings.h"
#include "settings_menu.h"
#include "settings_store.h"
#include "demo_mode.h"
#include "serial_commands.h"
#include "rotary_encoder.h"

CRGB kblLEDs[okbl::leds::NUM_LEDS];

KeyState keyStates[okbl::leds::NUM_LEDS];

// works out which hand pressed each Key, for Color::Hands; follows keyStates
okbl::hands::Tracker handTracker(okbl::hands::CONFIG);

bool sustainPedalDown = false;

okbl::Settings settings  = okbl::lights::DEFAULT_SETTINGS;
uint8_t masterBrightness = okbl::lights::MASTER_BRIGHTNESS_STEPS;
uint8_t masterScale      = 255; // FastLED global brightness for masterBrightness, before power limiting

// Lowest global brightness at which a full-value channel still reaches the strip as
// MIN_PEAK; any dimmer and the floor can't be met. FastLED sends (255 * (scale + 1)) >> 8.
static const uint8_t MIN_MASTER_SCALE = (okbl::leds::MIN_PEAK * 256 + 254) / 255 - 1;
uint32_t powerBudgetMW                = okbl::leds::PSU_MAX_MA * okbl::leds::PSU_VOLTAGE;
uint8_t lastScale                     = 255; // global brightness the last frame went out with

// Smallest buffer value that still reaches the strip as at least MIN_PEAK under global
// brightness `scale`. FastLED sends (value * (scale + 1)) >> 8; dithering only adds to that.
uint8_t floorBeforeScale(uint8_t scale) {
    uint16_t value = ((uint16_t) okbl::leds::MIN_PEAK * 256 + scale) / (scale + 1); // rounded up
    return min<uint16_t>(value, 255);
}

// The floor for Pedal Fade to aim at, from the last frame's global brightness (this frame's
// isn't known until its LEDs are rendered).
uint8_t currentFloor() {
    return floorBeforeScale(lastScale);
}

// While a preview runs it takes over every LED; key state keeps updating underneath.
CRGB previewLEDs[okbl::leds::NUM_LEDS];
uint32_t previewStartMs    = 0;
uint32_t previewDurationMs = 0; // 0 = no preview running

// previewLEDs must already be filled
void startPreview(uint32_t durationMs) {
    previewStartMs    = millis();
    previewDurationMs = durationMs;
}

bool previewRunning() {
    if (previewDurationMs && millis() - previewStartMs >= previewDurationMs) {
        previewDurationMs = 0;
    }
    return previewDurationMs != 0;
}

// Renders the current Settings from simulated keys, so the Preview always matches them.
void showPreview() {
    KeyState keys[okbl::leds::NUM_LEDS];
    okbl::render::previewKeys(keys, settings);
    bool blank[okbl::leds::NUM_LEDS];
    okbl::render::computeTargets(previewLEDs, blank, keys, settings, millis(), currentFloor());
    startPreview(okbl::lights::PREVIEW_MS);
}

okbl::Settings &currentSettings() {
    return settings;
}

// Settings changed in the menu are saved when it closes; others (serial `set`, the encoder's
// hue) are saved straight away, subject to the store's rate limit.
void saveSettingsUnlessMenuOpen() {
    if (!okbl::menu::isOpen())
        okbl::store::requestSave();
}

void onSettingsChanged() {
    debugf("Settings: ");
    okbl::menu::printSettings(settings);
    saveSettingsUnlessMenuOpen();
    showPreview();
}

void showBrightnessBar() {
    // Bar length in 1/256ths of an LED, so every detent moves it the same distance even though
    // a step is a fraction of an LED. The LED at the tip is lit in proportion to how much of it
    // the bar covers (the brightness floor keeps it visible).
    uint32_t length = (uint32_t) masterBrightness * okbl::leds::NUM_LEDS * 256 / okbl::lights::MASTER_BRIGHTNESS_STEPS;
    for (uint8_t i = 0; i < okbl::leds::NUM_LEDS; i++) {
        uint32_t start  = (uint32_t) i * 256;
        uint8_t covered = length >= start + 255 ? 255 : length > start ? length - start : 0;
        previewLEDs[i]  = covered ? CRGB(CHSV(settings.baseHue, 255, covered)) : CRGB(CRGB::Black);
    }
    startPreview(okbl::lights::BRIGHTNESS_BAR_MS);
}

void setMasterBrightness(int level) {
    masterBrightness = constrain(level, 1, okbl::lights::MASTER_BRIGHTNESS_STEPS);
    // evenly spaced from MIN_MASTER_SCALE (level 1) to 255 (top level)
    masterScale = MIN_MASTER_SCALE + (255 - MIN_MASTER_SCALE) * (masterBrightness - 1) / (okbl::lights::MASTER_BRIGHTNESS_STEPS - 1);
    debugf("Master brightness: %d/%d (scale %d)\n", masterBrightness, okbl::lights::MASTER_BRIGHTNESS_STEPS, masterScale);
    showBrightnessBar();
}

// Color::BaseHue's hue; a uint8_t, so turning the encoder wraps all the way round
void setBaseHue(uint8_t hue) {
    settings.baseHue = hue;
    debugf("Base hue: %d\n", hue);
    saveSettingsUnlessMenuOpen();
    showPreview();
}

void clearKeys() {
    for (KeyState &key : keyStates) {
        key = {};
    }
    handTracker.reset();
}

void onDemoModeStart() {
    clearKeys();
    sustainPedalDown = false;
}

void onMenuOpen() {
    clearKeys();
    okbl::demo::stop();
    okbl::demo::recordActivity();
}

void onMenuClose() {
    okbl::store::requestSave();
}

// Brightness::Accent: whether a note is much harder than the running average of the notes before it; then
// adds it to that average. The average is in 1/16ths of a velocity step.
uint16_t velocityAverage16 = okbl::accent::START_AVERAGE * 16;

bool isAccented(uint8_t velocity) {
    bool accented = velocity * 16 >= velocityAverage16 + okbl::accent::MARGIN * 16;
    velocityAverage16 += ((int) velocity * 16 - (int) velocityAverage16) / okbl::accent::AVERAGE_NOTES;
    return accented;
}

// returns -1 for notes outside the 88-key range
int noteToKeyIndex(byte note) {
    if (note < okbl::keys::MIDI_NOTE_LOWEST || note > okbl::keys::MIDI_NOTE_HIGHEST) {
        return -1;
    }
    return note - okbl::keys::MIDI_NOTE_LOWEST;
}

void handleNoteOn(byte channel, byte note, byte velocity) {
    int i = noteToKeyIndex(note);
    if (i < 0)
        return;

#ifdef KBL_USB_MIDI_DEBUG
    debugf("Note: %d\n", note);
#endif
    okbl::demo::recordActivity();
    if (okbl::menu::isOpen() && okbl::menu::onKeyDown(i))
        return;
    okbl::demo::stop();

    // Blank On Press: a Key whose LED is already lit goes dark briefly, so the press shows
    keyStates[i].blanking  = isSounding(keyStates[i]) || kblLEDs[i];
    keyStates[i].pressedMs = millis();
    keyStates[i].accented  = isAccented(velocity);
    keyStates[i].held      = true;
    keyStates[i].sustained = false;
    keyStates[i].velocity  = velocity;
    keyStates[i].hand      = handTracker.press(i, velocity);

    // a new Key can show that an earlier guess was wrong
    for (uint8_t m = 0; m < handTracker.movedCount(); m++) {
        uint8_t k         = handTracker.moved()[m];
        keyStates[k].hand = handTracker.handOf(k);
#ifdef KBL_HAND_DEBUG
        keyStates[k].correctedMs = millis() | 1; // never 0, which means never corrected
        debugf("Hand: key %d corrected to %s\n", k, keyStates[k].hand == okbl::hands::Hand::Left ? "left" : "right");
#endif
    }
}

// the MIDI library also routes note-on with velocity 0 here
void handleNoteOff(byte channel, byte note, byte velocity) {
    int i = noteToKeyIndex(note);
    if (i < 0)
        return;

    okbl::demo::recordActivity();

    // nothing to release: the key was pressed while the Settings Menu was open, or was
    // cleared when the menu or Demo Mode started
    if (!keyStates[i].held)
        return;
    keyStates[i].held = false;
    handTracker.release(i);
    keyStates[i].sustained  = sustainPedalDown;
    keyStates[i].releasedMs = millis();
}

void handleControlChange(byte channel, byte number, byte value) {
    if (number == okbl::midi::CC_ALL_SOUND_OFF || number == okbl::midi::CC_ALL_NOTES_OFF) {
        clearKeys();
        return;
    }
    if (number != okbl::midi::CC_SUSTAIN)
        return;

    okbl::demo::recordActivity();

    sustainPedalDown = value >= 64;
    if (sustainPedalDown) {
        okbl::demo::stop();
    }
    else {
        for (KeyState &key : keyStates) {
            key.sustained = false;
        }
    }
}

void handleRotary() {
    RotaryEvents rotary = rotaryPoll();
    if (rotary.steps) {
        // under Color::BaseHue the encoder turns the hue; Master Brightness stays where it was
        if (settings.color == okbl::Color::BaseHue)
            setBaseHue(settings.baseHue + rotary.steps * okbl::lights::BASE_HUE_STEP);
        else
            setMasterBrightness(masterBrightness + rotary.steps);
    }
    if (rotary.presses) {
        okbl::menu::toggle();
    }
}

// Keeps lit LEDs out of the flickering range: those below the floor are raised to it,
// keeping their color.
void raiseToFloor(const CRGB *target, uint8_t scale) {
    const uint8_t floorPeak = floorBeforeScale(scale);
    for (uint8_t i = 0; i < okbl::leds::NUM_LEDS; i++) {
        CRGB &led    = kblLEDs[i];
        uint8_t peak = max(led.r, max(led.g, led.b));
        if (!target[i] || peak == 0 || peak >= floorPeak)
            continue;
        led = CRGB((led.r * floorPeak + peak / 2) / peak, (led.g * floorPeak + peak / 2) / peak, (led.b * floorPeak + peak / 2) / peak);
    }
}

// Fading LEDs below the floor would flicker, so they fade out quickly (a short tail rather than
// a cut to black). Returns true if any are in that tail, so the frame goes out undithered.
bool hurryFadeBelowFloor(const CRGB *target, uint8_t scale) {
    const uint8_t floorPeak = floorBeforeScale(scale);
    bool any                = false;
    for (uint8_t i = 0; i < okbl::leds::NUM_LEDS; i++) {
        CRGB &led    = kblLEDs[i];
        uint8_t peak = max(led.r, max(led.g, led.b));
        if (target[i] || peak == 0 || peak >= floorPeak)
            continue;
        led.fadeToBlackBy(okbl::lights::BELOW_FLOOR_FADE_PER_FRAME);
        any = true;
    }
    return any;
}

// Fills kblLEDs and returns the global brightness to show them with. dither is set false
// while any LED is fading out below the brightness floor.
uint8_t renderKeys(bool &dither) {
    CRGB frame[okbl::leds::NUM_LEDS];
    bool blank[okbl::leds::NUM_LEDS] = {};
    const CRGB *target               = frame;
    if (previewRunning()) {
        target = previewLEDs;
    }
    else if (okbl::menu::isOpen()) {
        okbl::menu::render(frame, millis());
    }
    else {
        okbl::render::computeTargets(frame, blank, okbl::demo::active() ? okbl::demo::keys() : keyStates, settings, millis(), currentFloor());
    }

    for (uint8_t i = 0; i < okbl::leds::NUM_LEDS; i++) {
        if (target[i]) {
            kblLEDs[i] = target[i];
        }
        else if (blank[i]) {
            kblLEDs[i] = CRGB::Black; // Blank On Press: straight off, not a fade
        }
        else {
            kblLEDs[i].fadeToBlackBy(okbl::lights::FADE_PER_FRAME);
        }
    }

    // Power limiting is done here rather than by FastLED, so the floor can allow for it.
    uint8_t requested = okbl::menu::isOpen() ? max(masterScale, okbl::menu::MIN_GLOBAL_BRIGHTNESS) : masterScale;
    uint8_t scale     = calculate_max_brightness_for_power_mW(kblLEDs, okbl::leds::NUM_LEDS, requested, powerBudgetMW);
    raiseToFloor(target, scale);
    // raising dim LEDs adds a little power; settle once more
    uint8_t settled = calculate_max_brightness_for_power_mW(kblLEDs, okbl::leds::NUM_LEDS, scale, powerBudgetMW);
    if (settled < scale) {
        scale = settled;
        raiseToFloor(target, scale);
    }
    dither    = !hurryFadeBelowFloor(target, scale);
    lastScale = scale;
    return scale;
}

void setup() {

    debugStart();
    debugWaitForInterface();

    debugf("Serial Initialized");

    // connected to high side of optocoupler w/ external pullup.
    // todo: does library configure?
    pinMode(okbl::pins::MIDI_RX, INPUT);

    rotaryBegin();

    // pinMode(okbl::pins::RGB_DATA, OUTPUT); // configured by fastled

    // pinMode(okbl::pins::ENABLE_PSU, OUTPUT); // shorted high on first revision, leave like this
    digitalWrite(okbl::pins::ENABLE_PSU, HIGH);

    debugf("Pinout Initialized");

    FastLED.addLeds<WS2812B, okbl::pins::RGB_DATA, EOrder::GRB>(kblLEDs, okbl::leds::NUM_LEDS);

    if (PCB_REVISION == 0) {
        // the board may or may not be running on USB, so we should limit current to around 500mA
        // I'll uncomment this line when I deploy the board
        // Alternative smart thing to do: assume the PSU is connected, but if we detect a PC connection at any point during operation,
        // limit draw to 500mA.
        powerBudgetMW = okbl::leds::USB_MAX_MA * okbl::leds::PSU_VOLTAGE;
    }

    okbl::menu::begin();
    okbl::store::begin(settings);

    debugf("Settings: ");
    okbl::menu::printSettings(settings);
    showPreview();

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
    while (MIDI.read()) {
    }
    serialCommandsPoll(); // no-op unless KBL_USB_MIDI_DEBUG
    handleRotary();

    okbl::demo::poll();
    okbl::menu::poll();
    okbl::store::poll(settings);

    EVERY_N_MILLISECONDS(okbl::lights::FRAME_MS) {
        bool dither;
        uint8_t scale = renderKeys(dither);
        FastLED.setDither(dither ? BINARY_DITHER : DISABLE_DITHER);
        FastLED.show(scale);
    }

    EVERY_N_SECONDS(okbl::debug::FPS_REPORT_S) {
        debugf("FastLED at %d fps\n", FastLED.getFPS());
    }
}

#include <Arduino.h>
#include <FastLED.h>

#include "app_conf.h"
#include "hardware.h"
#include "midi_util.h"
#include "key_state.h"
#include "render.h"
#include "settings.h"
#include "settings_menu.h"
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

kbl::Settings settings = kbl::lights::DEFAULT_SETTINGS;
uint8_t masterBrightness = kbl::lights::MASTER_BRIGHTNESS_STEPS;
uint8_t masterScale = 255; // FastLED global brightness for masterBrightness, before power limiting

// Lowest global brightness at which a full-value channel still reaches the strip as
// MIN_PEAK; any dimmer and the floor can't be met. FastLED sends (255 * (scale + 1)) >> 8.
static const uint8_t MIN_MASTER_SCALE = (kbl::leds::MIN_PEAK * 256 + 254) / 255 - 1;
uint32_t powerBudgetMW = kbl::leds::PSU_MAX_MA * kbl::leds::PSU_VOLTAGE;
uint8_t lastScale = 255; // global brightness the last frame went out with

// Smallest buffer value that still reaches the strip as at least MIN_PEAK under global
// brightness `scale`. FastLED sends (value * (scale + 1)) >> 8; dithering only adds to that.
uint8_t floorBeforeScale(uint8_t scale)
{
    uint16_t value = ((uint16_t)kbl::leds::MIN_PEAK * 256 + scale) / (scale + 1); // rounded up
    return min<uint16_t>(value, 255);
}

// The floor for Pedal Fade to aim at, from the last frame's global brightness (this frame's
// isn't known until its LEDs are rendered). Sag compensation dims LEDs nearer the supply by
// up to ~2% more; raiseToFloor() holds those at the floor for the last moments.
uint8_t currentFloor()
{
    return floorBeforeScale(lastScale);
}

// While a preview runs it takes over every LED; key state keeps updating underneath.
CRGB previewLEDs[kbl::leds::NUM_LEDS];
uint32_t previewStartMs = 0;
uint32_t previewDurationMs = 0; // 0 = no preview running

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

// Renders the current Settings from simulated keys, so the Preview always matches them.
void showPreview()
{
    KeyState keys[kbl::leds::NUM_LEDS];
    kbl::render::previewKeys(keys, settings);
    kbl::render::computeTargets(previewLEDs, keys, settings, millis(), currentFloor());
    startPreview(kbl::lights::PREVIEW_MS);
}

kbl::Settings &currentSettings()
{
    return settings;
}

void onSettingsChanged()
{
    debugf("Settings: ");
    kbl::menu::printSettings(settings);
    showPreview();
}

void showBrightnessBar()
{
    // Bar length in 1/256ths of an LED, so every detent moves it the same distance even though
    // a step is a fraction of an LED. The LED at the tip is lit in proportion to how much of it
    // the bar covers (the brightness floor keeps it visible).
    uint32_t length = (uint32_t)masterBrightness * kbl::leds::NUM_LEDS * 256 / kbl::lights::MASTER_BRIGHTNESS_STEPS;
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++)
    {
        uint32_t start = (uint32_t)i * 256;
        uint8_t covered = length >= start + 255 ? 255 : length > start ? length - start : 0;
        previewLEDs[i] = covered ? CRGB(CHSV(kbl::lights::BASE_HUE, 255, covered)) : CRGB(CRGB::Black);
    }
    startPreview(kbl::lights::BRIGHTNESS_BAR_MS);
}

void setMasterBrightness(int level)
{
    masterBrightness = constrain(level, 1, kbl::lights::MASTER_BRIGHTNESS_STEPS);
    // evenly spaced from MIN_MASTER_SCALE (level 1) to 255 (top level)
    masterScale = MIN_MASTER_SCALE + (255 - MIN_MASTER_SCALE) * (masterBrightness - 1) / (kbl::lights::MASTER_BRIGHTNESS_STEPS - 1);
    debugf("Master brightness: %d/%d (scale %d)\n", masterBrightness, kbl::lights::MASTER_BRIGHTNESS_STEPS, masterScale);
    showBrightnessBar();
}

void clearKeys()
{
    for (KeyState &key : keyStates)
    {
        key = {};
    }
}

void onDemoModeStart()
{
    clearKeys();
    sustainPedalDown = false;
}

void onMenuOpen()
{
    clearKeys();
    kbl::demo::stop();
    kbl::demo::recordActivity();
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

void handleNoteOn(byte channel, byte note, byte velocity)
{
    int i = noteToKeyIndex(note);
    if (i < 0)
        return;

#ifdef KBL_USB_MIDI_DEBUG
    debugf("Note: %d\n", note);
#endif
    kbl::demo::recordActivity();
    if (kbl::menu::isOpen() && kbl::menu::onKeyDown(i))
        return;
    kbl::demo::stop();

    keyStates[i].held = true;
    keyStates[i].sustained = false;
    keyStates[i].velocity = velocity;
}

// the MIDI library also routes note-on with velocity 0 here
void handleNoteOff(byte channel, byte note, byte velocity)
{
    int i = noteToKeyIndex(note);
    if (i < 0)
        return;

    kbl::demo::recordActivity();

    // nothing to release: the key was pressed while the Settings Menu was open, or was
    // cleared when the menu or Demo Mode started
    if (!keyStates[i].held)
        return;
    keyStates[i].held = false;
    keyStates[i].sustained = sustainPedalDown;
    keyStates[i].releasedMs = millis();
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

void handleRotary()
{
    RotaryEvents rotary = rotaryPoll();
    if (rotary.steps)
    {
        setMasterBrightness(masterBrightness + rotary.steps);
    }
    if (rotary.presses)
    {
        kbl::menu::toggle();
    }
}


// Keeps lit LEDs out of the flickering range: those below the floor are raised to it,
// keeping their color.
void raiseToFloor(const CRGB *target, uint8_t scale)
{
    const uint8_t floorPeak = floorBeforeScale(scale);
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++)
    {
        CRGB &led = kblLEDs[i];
        uint8_t peak = max(led.r, max(led.g, led.b));
        if (!target[i] || peak == 0 || peak >= floorPeak)
            continue;
        led = CRGB((led.r * floorPeak + peak / 2) / peak,
                   (led.g * floorPeak + peak / 2) / peak,
                   (led.b * floorPeak + peak / 2) / peak);
    }
}

// Fading LEDs below the floor would flicker, so they fade out quickly (a short tail rather than
// a cut to black). Returns true if any are in that tail, so the frame goes out undithered.
bool hurryFadeBelowFloor(const CRGB *target, uint8_t scale)
{
    const uint8_t floorPeak = floorBeforeScale(scale);
    bool any = false;
    for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++)
    {
        CRGB &led = kblLEDs[i];
        uint8_t peak = max(led.r, max(led.g, led.b));
        if (target[i] || peak == 0 || peak >= floorPeak)
            continue;
        led.fadeToBlackBy(kbl::lights::BELOW_FLOOR_FADE_PER_FRAME);
        any = true;
    }
    return any;
}

// Fills kblLEDs and returns the global brightness to show them with. dither is set false
// while any LED is fading out below the brightness floor.
uint8_t renderKeys(bool &dither)
{
    CRGB frame[kbl::leds::NUM_LEDS];
    const CRGB *target = frame;
    if (previewRunning())
    {
        target = previewLEDs;
    }
    else if (kbl::menu::isOpen())
    {
        kbl::menu::render(frame, millis());
    }
    else
    {
        kbl::render::computeTargets(frame, kbl::demo::active() ? kbl::demo::keys() : keyStates, settings, millis(), currentFloor());
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
    uint8_t requested = kbl::menu::isOpen() ? max(masterScale, kbl::menu::MIN_GLOBAL_BRIGHTNESS) : masterScale;
    uint8_t scale = calculate_max_brightness_for_power_mW(kblLEDs, kbl::leds::NUM_LEDS, requested, powerBudgetMW);
    raiseToFloor(target, scale);
    // raising dim LEDs adds a little power; settle once more
    uint8_t settled = calculate_max_brightness_for_power_mW(kblLEDs, kbl::leds::NUM_LEDS, scale, powerBudgetMW);
    if (settled < scale)
    {
        scale = settled;
        raiseToFloor(target, scale);
    }
    dither = !hurryFadeBelowFloor(target, scale);
    lastScale = scale;
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
    kbl::menu::begin();

    debugf("Settings: ");
    kbl::menu::printSettings(settings);
    showPreview();

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
    handleRotary();

    kbl::demo::poll();
    kbl::menu::poll();

    EVERY_N_MILLISECONDS(kbl::lights::FRAME_MS)
    {
        bool dither;
        uint8_t scale = renderKeys(dither);
        FastLED.setDither(dither ? BINARY_DITHER : DISABLE_DITHER);
        FastLED.show(scale);
    }

    EVERY_N_SECONDS(kbl::debug::FPS_REPORT_S)
    {
        debugf("FastLED at %d fps\n", FastLED.getFPS());
    }
}

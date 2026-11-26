// Power draw test. Build and flash with: pio run -e power_draw -t upload
//
// Steps the LEDs through known levels while a bench PSU logs the board's current draw. It runs
// once, from power-on, with no input: scripts/power_draw_log.py turns the PSU on, which starts
// it, and logs the draw. The script mirrors this file's timeline, so keep the two in step.
//
// A level is the value each lit channel gets: a red LED at level 40 is (40, 0, 0), a white one
// (40, 40, 40). Color correction, dithering and power limiting are all off, and global
// brightness is 255, so the level is exactly what reaches the strip.
//
// The timeline, from the end of setup():
//   1. Sync: every LED white at SYNC_LEVEL for SYNC_MS. Its falling edge in the PSU log is where
//      the script lines the rest of the timeline up, since boot time and the PSU's measurement
//      lag are both unknown. The PSU has no readings yet when it starts, so its rising edge
//      can't be used.
//   2. Baseline: all LEDs off for OFF_MS, to measure the board with nothing lit.
//   3. For each color in SWEEP_COLORS:
//      Single: LED 0 (A0) steps through levels 1 to 255, STEP_MS per level.
//      Gap:    all LEDs off for OFF_MS.
//      All:    every LED steps through levels 1 to 255, STEP_MS per level.
//      Gap:    all LEDs off for OFF_MS.
//   4. Done: all LEDs off until power-off.
//
// White comes last because all LEDs at high white levels may pass the script's cut-off current,
// and the cut-off ends the run.
//
// No power limit: all LEDs at white 255 is about 88 * 34 mA = 3 A, and up to 5.3 A with LEDs
// that draw 60 mA.
//
// No USB is connected while it runs, so it prints nothing.

#include <Arduino.h>
#include <FastLED.h>

#include "app_conf.h"
#include "hardware.h"

namespace
{
    const uint32_t STEP_MS    = 1000;  // time each level is held; the PSU's readings lag up to about a second
    const uint32_t OFF_MS     = 10000; // length of the baseline and of each gap
    const uint32_t SYNC_MS    = 2000;
    const uint8_t SYNC_LEVEL  = 32; // about 0.7 A across all LEDs: clear of boot noise, far below the cut-off
    const uint32_t SWEEP_MS   = 255 * STEP_MS;
    const CRGB SWEEP_COLORS[] = {CRGB(1, 0, 0), CRGB(0, 1, 0), CRGB(0, 0, 1), CRGB(1, 1, 1)}; // red, green, blue, white

    enum class Kind : uint8_t {
        Off,
        Sync,
        Single,
        All,
    };

    struct Segment {
        Kind kind;
        CRGB color; // channels that light, each 0 or 1; unused by Off and Sync
        uint32_t durationMs;
    };

    const uint8_t NUM_SWEEP_COLORS = sizeof(SWEEP_COLORS) / sizeof(SWEEP_COLORS[0]);
    const uint8_t NUM_SEGMENTS     = 2 + 4 * NUM_SWEEP_COLORS;

    CRGB leds[okbl::leds::NUM_LEDS];
    Segment timeline[NUM_SEGMENTS];
    uint32_t startMs = 0; // millis() when the timeline began

    void buildTimeline() {
        uint8_t n     = 0;
        timeline[n++] = {Kind::Sync, CRGB::Black, SYNC_MS};
        timeline[n++] = {Kind::Off, CRGB::Black, OFF_MS};
        for (const CRGB &color : SWEEP_COLORS) {
            timeline[n++] = {Kind::Single, color, SWEEP_MS};
            timeline[n++] = {Kind::Off, CRGB::Black, OFF_MS};
            timeline[n++] = {Kind::All, color, SWEEP_MS};
            timeline[n++] = {Kind::Off, CRGB::Black, OFF_MS};
        }
    }

    // fills the LED buffer for `elapsed` ms into the timeline; everything is off once it's over
    void render(uint32_t elapsed) {
        FastLED.clear();
        for (const Segment &segment : timeline) {
            if (elapsed >= segment.durationMs) {
                elapsed -= segment.durationMs;
                continue;
            }
            uint8_t level = elapsed / STEP_MS + 1;
            switch (segment.kind) {
            case Kind::Off:
                break;
            case Kind::Sync:
                fill_solid(leds, okbl::leds::NUM_LEDS, CRGB(SYNC_LEVEL, SYNC_LEVEL, SYNC_LEVEL));
                break;
            case Kind::Single:
                leds[0] = CRGB(segment.color.r * level, segment.color.g * level, segment.color.b * level);
                break;
            case Kind::All:
                fill_solid(leds, okbl::leds::NUM_LEDS, CRGB(segment.color.r * level, segment.color.g * level, segment.color.b * level));
                break;
            }
            return;
        }
    }
} // namespace

void setup() {
    digitalWrite(okbl::pins::ENABLE_PSU, HIGH);

    FastLED.addLeds<WS2812B, okbl::pins::RGB_DATA, EOrder::GRB>(leds, okbl::leds::NUM_LEDS);
    FastLED.setCorrection(UncorrectedColor);
    FastLED.setTemperature(UncorrectedTemperature);
    FastLED.setDither(DISABLE_DITHER);
    FastLED.setBrightness(255);
    FastLED.clear(true);

    buildTimeline();
    startMs = millis();
}

void loop() {
    // re-send at the normal frame rate, like the real firmware does, so the data line's share
    // of the draw matches normal use
    EVERY_N_MILLISECONDS(okbl::lights::FRAME_MS) {
        render(millis() - startMs);
        FastLED.show();
    }
}

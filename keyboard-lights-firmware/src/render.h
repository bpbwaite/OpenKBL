#pragma once

#include <Arduino.h>
#include <FastLED.h>

#include "key_state.h"
#include "settings.h"

// Turns key state into LED colors under the current Settings.

namespace okbl
{
    namespace render
    {
        // Fills target (NUM_LEDS long) with what each LED should show. Black = not lit (the LED
        // fades out). Runs in two steps so the Settings stay independent: the Light Mode decides
        // which LEDs light and how hard, then Color and Brightness shade each lit LED.
        // nowMs drives Pedal Fade, which fades a Sustained Key down to floorPeak: the dimmest
        // target peak (largest of R, G, B) that still shows at the current global brightness.
        // blank (NUM_LEDS long) is set for LEDs that Blank On Press holds dark: they should go
        // straight off rather than fade.
        void computeTargets(CRGB *target, bool *blank, const KeyState *keys, const Settings &s, uint32_t nowMs, uint8_t floorPeak);

        // Fills keys (NUM_LEDS long) with simulated keys that show off the current Settings,
        // for the Preview.
        void previewKeys(KeyState *keys, const Settings &s);
    } // namespace render
} // namespace okbl

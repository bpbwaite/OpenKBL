#pragma once

#include <Arduino.h>

#include "key_state.h"

// Demo Mode: LEDs light at random under the current Light Mode with no Keys pressed,
// until someone plays. Starts by itself after kbl::demo::IDLE_MS without playing, or
// on request (rotary press, `demo` serial command). Configured in app_conf.h.

// defined in main.cpp: reset live key state when Demo Mode starts, which also clears
// stuck keys for when the piano never sends a note-off
void onDemoModeStart();

namespace kbl {
    namespace demo {
        void start();
        void stop();
        bool active();

        // a key or pedal event: restarts the idle timer, but doesn't stop Demo Mode
        void recordActivity();

        // non-blocking; call every loop. Starts Demo Mode when idle and advances the simulated keys.
        void poll();

        // Simulated keys, kept apart from the live keys so they can't trigger Mode Select.
        // NUM_LEDS long; all released while Demo Mode is off.
        const KeyState *keys();
    }
}

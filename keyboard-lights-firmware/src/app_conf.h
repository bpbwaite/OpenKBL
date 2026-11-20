#pragma once

#include <Arduino.h>
#include <FastLED.h>

#include "light_modes.h"
#include "chord_modes.h"

// All tunable behaviour lives here. Values fixed by the PCB or the piano are in hardware.h.

// ---- serial debug interface ----

#define BAUDRATE_ESP_SERIAL (115200)
#define DEBUG_WAIT_MS (3000) // how long to wait for a serial monitor at boot

// Serial is the C3's USB CDC (HWCDC). With a 0 ms TX timeout, prints are dropped
// instead of blocking when the host isn't reading (e.g. USB plugged, monitor closed).
#define debugStart() {Serial.begin(BAUDRATE_ESP_SERIAL); Serial.setTxTimeoutMs(0);}
// bounded so the board still boots without a USB host
#define debugWaitForInterface() {unsigned long t0 = millis(); while (!Serial && millis() - t0 < DEBUG_WAIT_MS) { yield(); }}

#define debugf(...) Serial.printf(__VA_ARGS__)

namespace kbl {
    namespace debug {
        static const uint32_t FPS_REPORT_S = 10; // how often the frame rate is printed
    }

    // ---- lights ----
    namespace lights {
        static const LightMode DEFAULT_LIGHT_MODE = LightMode::PitchHue;
        static const uint32_t FRAME_MS = 10;      // ~100 fps; fade speed depends on this
        static const uint8_t FADE_PER_FRAME = 40; // out of 255, applied to released keys
        static const uint8_t BASE_HUE = HUE_AQUA;
        static const uint8_t MASTER_BRIGHTNESS_STEPS = 16; // lowest step keeps the softest notes visible
        static const uint32_t MODE_PREVIEW_MS = 500;
        static const uint32_t BRIGHTNESS_BAR_MS = 1000;
    }

    // ---- key colors ----
    // Colors for LightMode::KeyColors and LightMode::AllOctaves: one 0xRRGGBB hex code per
    // PitchClass, used in every octave. Brightness still follows velocity.
    namespace keycolors {
        // TODO: pick new colors for D# / Eb and G# / Ab
        static const uint32_t PITCH_CLASS_COLORS[12] = {
            0xFE621A, // C - orange
            0x25D4AA, // C# / Db - turquois
            0x2ACEEB, // D - sky blue
            0x80FF00, // D# / Eb
            0xEAEB1C, // E - yellow
            0x18E017, // F - lime green
            0x63BF5C, // F# / Gb - deep green
            0xD425B2, // G - magenta. not sure it's a good fit
            0xFF5010, // G# / Ab - reddish orange
            0xF9000B, // A - blood red
            0xB12DE3, // A# / Bb - purple
            0x1B16A1, // B - navy blue
        };
    }

    // ---- Mode Select ----
    // Press MODE_SELECT_KEY on its own, hold it for at least MODE_SELECT_HOLD_MS, then play
    // one of the chords below. Chords are octave-independent: only the set of pitch classes
    // matters, so any voicing or inversion works. The chord must match exactly (no extra keys
    // held, other than MODE_SELECT_KEY itself). Chords avoid natural A, which is likely to be
    // played over an A0 bass note.
    namespace chords {
        static const uint8_t MODE_SELECT_KEY = 0; // key index, 0 = A0
        static const uint32_t MODE_SELECT_HOLD_MS = 100;

        static const ChordMode CHORD_MODES[] = {
            { chord({C, E, G}), LightMode::VelocityBrightness }, // C major
            { chord({F, Ab, C}), LightMode::FullBrightness },    // F minor
            { chord({G, B, D}), LightMode::PitchHue },           // G major
            { chord({Ab, C, Eb}), LightMode::KeyColors },        // Ab major
            { chord({E, G, B}), LightMode::AllOctaves },         // E minor
        };
    }

    // ---- Demo Mode ----
    namespace demo {
        static const uint32_t IDLE_MS = 5 * 60 * 1000; // starts by itself after this long without playing
        static const uint32_t ROLL_MS = 50;
        static const uint16_t ROLL_MAX = 400;          // a roll below NUM_LEDS lights that key, so higher = sparser
        static const uint32_t KEY_MS = 1000;
    }

    // ---- USB serial test commands (KBL_USB_MIDI_DEBUG) ----
    namespace serialcmd {
        static const size_t MAX_LINE_CHARS = 64;
        static const size_t MAX_NOTES = 16;
        static const uint8_t DEFAULT_VELOCITY = 100;
    }
}

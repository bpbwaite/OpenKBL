#pragma once

#include <Arduino.h>
#include <FastLED.h>

#include "settings.h"

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
        static const Settings DEFAULT_SETTINGS = {
            LightMode::PressedKey,
            Color::PitchHue,
            Brightness::Velocity,
            PedalFade::Off,
        };
        // Pedal Fade: a Sustained Key dims at a steady rate (equal halvings per second, which looks
        // even to the eye) down to the brightness floor at the current Master Brightness, then goes
        // dark. A full-brightness key takes SUSTAIN_FADE_MS; a softer key starts dimmer, so under
        // Brightness::Velocity it goes dark sooner.
        static const uint32_t SUSTAIN_FADE_MS = 10 * 1000;
        // Every Sustained Key stays lit at least this long, even one that starts at the floor
        // (a soft key, or any key at low Master Brightness), so the pedal still visibly holds it.
        static const uint32_t SUSTAIN_FADE_MIN_MS = 1500;
        static const uint32_t FRAME_MS = 10;      // ~100 fps; fade speed depends on this
        static const uint8_t FADE_PER_FRAME = 40; // out of 255, applied to released keys
        // Once a fading LED drops below the brightness floor it fades this much more per frame
        // (out of 255), with dithering off, instead of cutting straight to black. The tail is
        // short enough that the flicker below the floor isn't seen.
        static const uint8_t BELOW_FLOOR_FADE_PER_FRAME = 96;
        static const uint8_t BASE_HUE = HUE_AQUA;
        static const uint8_t MASTER_BRIGHTNESS_STEPS = 48; // encoder detents from dimmest to full brightness
        static const uint32_t PREVIEW_MS = 500;
        static const uint32_t BRIGHTNESS_BAR_MS = 1000;
    }

    // ---- key colors ----
    // Colors for Color::KeyColors: one 0xRRGGBB hex code per PitchClass, used in every octave.
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

    // ---- Settings Menu ----
    // Opened and closed with the encoder switch. Which Settings it shows, and in what order, is
    // the GROUPS table in settings_menu.cpp.
    namespace menu {
        static const uint8_t FIRST_KEY = 0;           // key index of the first Option, 0 = A0 (below the encoder); must be a white key
        static const uint8_t GAP_WHITE_KEYS = 1;      // unlit white Keys between Settings; Options only use white Keys
        static const uint32_t TIMEOUT_MS = 30 * 1000; // closes by itself after this long without a key press
        static const uint8_t UNSELECTED_SCALE = 40;   // out of 255: brightness of unselected Options
        // Lowest FastLED global brightness (out of 255) the menu is drawn at, whatever the Master
        // Brightness, so selected and unselected Options stay distinguishable above the brightness floor.
        static const uint8_t MIN_GLOBAL_BRIGHTNESS = 96;

        // Swatch animation timing
        static const uint32_t SWATCH_PERIOD_MS = 1500;  // one cycle of the strike and ramp animations
        static const uint32_t RAINBOW_PERIOD_MS = 4000; // one trip around the hue wheel (PitchHue, Demo)
        static const uint32_t KEY_COLOR_STEP_MS = 400;  // how long KeyColors shows each PitchClass color
        static const uint32_t DEMO_TWINKLE_MS = 120;    // how often the Demo key's twinkle changes
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

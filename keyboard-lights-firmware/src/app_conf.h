#pragma once

#include <Arduino.h>
#include <FastLED.h>

#include "hands.h"
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

namespace okbl {
    namespace debug {
        static const uint32_t FPS_REPORT_S = 10; // how often the frame rate is printed
    }

    // ---- lights ----
    namespace lights {
        static const uint8_t DEFAULT_BASE_HUE = HUE_AQUA;
        // Under Color::BaseHue the encoder turns the hue this much per detent (256 = all the way round)
        static const uint8_t BASE_HUE_STEP = 4;
        // Color::PitchHue runs from red at the lowest key through this much of the hue wheel
        // (256 = all the way round) at the highest: 250 is ~352 degrees, so it stops short of red.
        static const uint8_t PITCH_HUE_SPAN = 250;
        static const Settings DEFAULT_SETTINGS = {
            LightMode::PressedKey,
            Color::PitchHue,
            Brightness::Velocity,
            PedalFade::On,
            BlankOnPress::On,
            DEFAULT_BASE_HUE,
        };
        // Pedal Fade: a Sustained Key dims at a steady rate (equal halvings per second, which looks
        // even to the eye) down to the brightness floor at the current Master Brightness, then goes
        // dark. A full-brightness key takes SUSTAIN_FADE_MS; a softer key starts dimmer, so under
        // Brightness::Velocity it goes dark sooner.
        static const uint32_t SUSTAIN_FADE_MS = 10 * 1000;
        // Every Sustained Key stays lit at least this long, even one that starts at the floor
        // (a soft key, or any key at low Master Brightness), so the pedal still visibly holds it.
        static const uint32_t SUSTAIN_FADE_MIN_MS = 1500;
        // Blank On Press: how long a pressed Key's already-lit LED is held off before it lights again
        static const uint32_t BLANK_ON_PRESS_MS = 50;
        static const uint32_t FRAME_MS = 10;      // ~100 fps; fade speed depends on this
        static const uint8_t FADE_PER_FRAME = 40; // out of 255, applied to released keys
        // Once a fading LED drops below the brightness floor it fades this much more per frame
        // (out of 255), with dithering off, instead of cutting straight to black. The tail is
        // short enough that the flicker below the floor isn't seen.
        static const uint8_t BELOW_FLOOR_FADE_PER_FRAME = 96;
        static const uint8_t MASTER_BRIGHTNESS_STEPS = 48; // encoder detents from dimmest to full brightness
        static const uint32_t PREVIEW_MS = 500;
        static const uint32_t BRIGHTNESS_BAR_MS = 1000;
    }

    // ---- harmonics ----
    // LightMode::Harmonics lights each Sounding Key's overtones as well, each dimmer than the one
    // below it. Where overtones of different Keys land on the same LED their brightness adds.
    namespace harmonics {
        struct Overtone {
            uint8_t semitones; // above the Sounding Key
            uint8_t level;     // brightness out of 255, relative to the Sounding Key
        };
        static const Overtone OVERTONES[] = {
            {12, 128}, // 2nd harmonic: the octave, 1/2
            {19, 85},  // 3rd: an octave and a fifth, 1/3
            {24, 64},  // 4th: two octaves, 1/4
        };
    }

    // ---- accent ----
    // Brightness::Accent: a note pressed much harder than the notes before it flashes white, then settles
    // into its color. "The notes before it" is a running average of their velocities.
    namespace accent {
        static const uint8_t MARGIN = 24;        // velocity above the running average that counts as accented
        static const uint8_t AVERAGE_NOTES = 8;  // roughly how many recent notes the average follows
        static const uint8_t START_AVERAGE = 64; // the average at boot, before any notes
        static const uint32_t FLASH_MS = 250;    // how long the white takes to settle into the color
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

    // ---- hands ----
    // Color::Hands works out which hand pressed each Key from the order of presses (hands.h).
    // It forgets where the hands were whenever Key state is cleared: the Settings Menu opens,
    // Demo Mode starts, or the piano sends All Notes Off.
    namespace hands {
        // With nothing else to go on, Keys from here up are the right hand's, below it the left's.
        static const uint8_t SPLIT_NOTE = 60; // MIDI note: C4, so B3 and below are left
        static const Config CONFIG = {
            SPLIT_NOTE - 21, // splitKey (21 = A0, the first key)
            17,              // maxSpan: an eleventh, in semitones, thumb to pinky
            // fingerSpan: semitones fingers 2-5 reach without the thumb. An octave suits
            // experienced players and larger hands; if Keys near a stretched hand's thumb land in
            // the wrong hand, try 10 (a minor seventh).
            12,
            2, // thumbPairSpan: the thumb can hold two Keys this close (C-D, E-F, C#-D#)
            // costs, in half-semitones of distance a hand moves
            24, // fingerOverCost: per semitone fingers 2-5 would stretch past fingerSpan
            8,  // thumbPairCost: the thumb holding two Keys, for a 6th Key in one hand
            24, // crossCost: the hands crossed over
            12, // idleCrossCost: a hand reaching past where the other, empty hand last played
            12, // flipCost: moving a Held Key to the other hand when correcting a guess
            3,  // busyCost: per Key beyond the first a hand already holds, when it takes another
            4,  // freshCost: first Key for a hand that hasn't played yet
            5,  // wrongSideCost: per half-semitone a fresh hand reaches past the Split Point
            16, // closeCall: a Key that won its hand by less than this can be moved by a later Key
            2,  // velocityTie: hands within this of each other go by the nearer velocity
        };
        static const uint32_t LEFT_COLOR = 0x0000FF;
        static const uint32_t RIGHT_COLOR = 0x00FF00;
        // With -DKBL_HAND_DEBUG, a Held Key whose hand was corrected flashes this color.
        static const uint32_t CORRECTED_COLOR = 0xFF0000;
        static const uint32_t CORRECTED_FLASH_MS = 400;

        // the hand by the Split Point alone, for simulated Keys
        inline Hand bySplitPoint(uint8_t keyIndex)
        {
            return keyIndex < CONFIG.splitKey ? Hand::Left : Hand::Right;
        }
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
        static const uint32_t HANDS_STEP_MS = 600;      // how long Hands shows each hand's color
        static const uint32_t DEMO_TWINKLE_MS = 120;    // how often the Demo key's twinkle changes
        // Blank On Press swatch: the dark gap between strikes, longer than the real one so it shows
        static const uint32_t BLANK_SWATCH_GAP_MS = 150;
    }

    // ---- saved Settings (settings_store.h) ----
    namespace store {
        // Settings are saved to flash at most once in this long, to limit wear
        static const uint32_t MIN_WRITE_INTERVAL_MS = 30 * 1000;
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

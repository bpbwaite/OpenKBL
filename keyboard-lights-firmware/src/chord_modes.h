#pragma once

#include <Arduino.h>
#include <initializer_list>

#include "light_modes.h"

// Mode selection by chord: press MODE_SELECT_KEY on its own, hold it for at least
// MODE_SELECT_HOLD_MS, then play one of the chords below.
// Chords are octave-independent: only the set of pitch classes matters, so any voicing
// or inversion works. The chord must match exactly (no extra keys held, other than
// MODE_SELECT_KEY itself). Chords avoid natural A, which is likely to be played over
// an A0 bass note.

namespace kbl {
    namespace chords {
        static const uint8_t MODE_SELECT_KEY = 0; // key index, 0 = A0
        static const uint32_t MODE_SELECT_HOLD_MS = 100;

        enum PitchClass : uint8_t {
            C = 0, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B,
            Db = Cs, Eb = Ds, Gb = Fs, Ab = Gs, Bb = As
        };

        // bitmask of pitch classes, bit 0 = C
        constexpr uint16_t chord(std::initializer_list<PitchClass> notes) {
            uint16_t mask = 0;
            for (PitchClass p : notes) {
                mask |= 1u << p;
            }
            return mask;
        }

        struct ChordMode {
            uint16_t chord;
            LightMode mode;
        };

        // ---- configuration ----
        static const ChordMode CHORD_MODES[] = {
            { chord({C, E, G}), LightMode::VelocityBrightness }, // C major
            { chord({F, Ab, C}), LightMode::FullBrightness },    // F minor
            { chord({G, B, D}), LightMode::PitchHue },           // G major
            { chord({Ab, C, Eb}), LightMode::KeyColors },        // Ab major
            { chord({E, G, B}), LightMode::AllOctaves },         // E minor
        };
    }
}

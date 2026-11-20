#pragma once

#include <Arduino.h>
#include <initializer_list>

#include "light_modes.h"

// Types for describing Mode Select chords. The chords themselves, and the Mode Select key,
// are configured in app_conf.h.

namespace kbl {
    namespace chords {
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
    }
}

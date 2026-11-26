#pragma once

#include <Arduino.h>

#include "hardware.h"

namespace okbl
{
    namespace pitch
    {
        enum PitchClass : uint8_t { C = 0, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B, Db = Cs, Eb = Ds, Gb = Fs, Ab = Gs, Bb = As };

        // PitchClass of a key, 0 = C ... 11 = B
        inline uint8_t keyPitchClass(uint8_t keyIndex) {
            return (keyIndex + keys::MIDI_NOTE_LOWEST) % 12;
        }

        inline bool isWhiteKey(uint8_t keyIndex) {
            switch (keyPitchClass(keyIndex)) {
            case Cs:
            case Ds:
            case Fs:
            case Gs:
            case As:
                return false;
            default:
                return true;
            }
        }
    } // namespace pitch
} // namespace okbl

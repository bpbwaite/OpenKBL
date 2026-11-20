#pragma once

#include <Arduino.h>

// Colors for LightMode::KeyColors and LightMode::AllOctaves: one 0xRRGGBB hex code per
// PitchClass, used in every octave. Brightness still follows velocity.

namespace kbl {
    namespace keycolors {
        // ---- configuration ----
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
}

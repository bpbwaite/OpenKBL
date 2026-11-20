#pragma once

#include <Arduino.h>

// Add new modes before COUNT, give them a name below, and handle them in main.cpp:
// keyColor() for modes that only light the pressed key, computeTargets() for modes that
// light other keys too. showModePreview() lights every key at full velocity unless the
// mode needs something else to represent it.
enum class LightMode : uint8_t {
    VelocityBrightness, // fixed hue, harder presses are brighter
    FullBrightness,     // fixed hue, always full brightness
    PitchHue,           // rainbow across the keyboard, velocity brightness
    KeyColors,          // per-PitchClass colors from app_conf.h, velocity brightness
    AllOctaves,         // pressing a key lights its PitchClass in every octave, app_conf.h key colors
    COUNT
};

inline const char* lightModeName(LightMode mode) {
    switch (mode) {
        case LightMode::VelocityBrightness: return "VelocityBrightness";
        case LightMode::FullBrightness:     return "FullBrightness";
        case LightMode::PitchHue:           return "PitchHue";
        case LightMode::KeyColors:          return "KeyColors";
        case LightMode::AllOctaves:         return "AllOctaves";
        default:                            return "Unknown";
    }
}

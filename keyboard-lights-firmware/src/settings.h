#pragma once

#include <Arduino.h>

// Settings: the independent choices that shape how LEDs respond, each holding one value.
// They're chosen in the Settings Menu (settings_menu.h) or over serial with `set`.
//
// Adding a Setting:
//   1. add its enum and a field to Settings below; an on/off Setting is an enum with Off and On
//   2. handle it in render.cpp
//   3. add a group, with one Option per value, to GROUPS in settings_menu.cpp; the menu layout,
//      the serial `set`/`settings` commands and the debug log all come from that table
//   4. give it a default in kbl::lights::DEFAULT_SETTINGS (app_conf.h)
// Adding an Option to an existing Setting: add it to the enum before COUNT, handle it in
// render.cpp, and add its Option (name and Swatch) to that group's table in settings_menu.cpp.

namespace kbl {
    // which LEDs light for the Sounding Keys
    enum class LightMode : uint8_t {
        PressedKey, // only the pressed key
        AllOctaves, // every key of a Sounding PitchClass
        COUNT
    };

    enum class Color : uint8_t {
        BaseHue,   // kbl::lights::BASE_HUE everywhere
        PitchHue,  // rainbow across the keyboard
        KeyColors, // per-PitchClass colors from kbl::keycolors
        Hands,     // one color per hand, from kbl::hands
        COUNT
    };

    enum class Brightness : uint8_t {
        Velocity, // harder presses are brighter
        Full,     // always full brightness
        COUNT
    };

    enum class PedalFade : uint8_t {
        Off, // Sustained Keys stay fully lit while the pedal is down
        On,  // Sustained Keys dim out over kbl::lights::SUSTAIN_FADE_MS after release
        COUNT
    };

    enum class BlankOnPress : uint8_t {
        Off, // pressing a Key whose LED is already lit leaves it lit
        On,  // ...turns that LED off for kbl::lights::BLANK_ON_PRESS_MS first, so every press shows
        COUNT
    };

    struct Settings {
        LightMode lightMode;
        Color color;
        Brightness brightness;
        PedalFade pedalFade;
        BlankOnPress blankOnPress;
    };
}

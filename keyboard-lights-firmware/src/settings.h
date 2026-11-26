#pragma once

#include <Arduino.h>

// Settings: the independent choices that shape how LEDs respond, each holding one value.
// They're chosen in the Settings Menu (settings_menu.h) or over serial with `set`.
//
// First consider adding an Option to an existing Setting instead (Brightness::Accent is Velocity
// plus a flash, not its own Off/On Setting). It costs one menu key instead of a group of two
// plus a gap, and it avoids pairs of Settings whose combinations don't all make sense.
//
// Adding a Setting:
//   1. add its enum and a field to Settings below; an on/off Setting is an enum with Off and On
//   2. handle it in render.cpp
//   3. add a group, with one Option per value, to GROUPS in settings_menu.cpp; the menu layout,
//      the serial `set`/`settings` commands and the debug log all come from that table
//   4. give it a default in okbl::lights::DEFAULT_SETTINGS (app_conf.h)
// A Setting that isn't a choice of Options (baseHue) is a plain field instead: it has no menu
// group, so give it a default, and print and save it alongside the groups.
// Adding an Option to an existing Setting: add it to the enum before COUNT, handle it in
// render.cpp, and add its Option (name and Swatch) to that group's table in settings_menu.cpp.
// Saved Settings (settings_store.h) hold each Option's index, so reordering Options changes
// what a saved value means; adding at the end doesn't.

namespace okbl
{
    // which LEDs light for the Sounding Keys
    enum class LightMode : uint8_t {
        PressedKey, // only the pressed key
        AllOctaves, // every key of a Sounding PitchClass
        Harmonics,  // the pressed key and, dimmer, its overtones (okbl::harmonics)
        COUNT
    };

    enum class Color : uint8_t {
        BaseHue,   // Settings::baseHue everywhere; the encoder turns the hue instead of Master Brightness
        PitchHue,  // rainbow across the keyboard
        KeyColors, // per-PitchClass colors from okbl::keycolors
        Hands,     // one color per hand, from okbl::hands
        COUNT
    };

    enum class Brightness : uint8_t {
        Full,     // always full brightness
        Velocity, // harder presses are brighter
        Accent,   // Velocity, and a note much harder than the recent ones flashes white, then settles (okbl::accent)
        COUNT
    };

    enum class PedalFade : uint8_t {
        Off, // Sustained Keys stay fully lit while the pedal is down
        On,  // Sustained Keys dim out over okbl::lights::SUSTAIN_FADE_MS after release
        COUNT
    };

    enum class BlankOnPress : uint8_t {
        Off, // pressing a Key whose LED is already lit leaves it lit
        On,  // ...turns that LED off for okbl::lights::BLANK_ON_PRESS_MS first, so every press shows
        COUNT
    };

    struct Settings {
        LightMode lightMode;
        Color color;
        Brightness brightness;
        PedalFade pedalFade;
        BlankOnPress blankOnPress;
        uint8_t baseHue; // FastLED hue (0-255) for Color::BaseHue
    };
} // namespace okbl

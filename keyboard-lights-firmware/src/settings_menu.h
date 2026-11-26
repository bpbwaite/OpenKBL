#pragma once

#include <Arduino.h>
#include <FastLED.h>

#include "settings.h"

// Settings Menu: opened with the encoder switch. While it's open, each white Key at the bass end
// is one Option of a Setting (or a Menu Action) and its LED shows that Option's Swatch: full
// brightness when selected, dim otherwise. Settings are laid out in GROUPS order from
// okbl::menu::FIRST_KEY, each taking one white Key per Option, with GAP_WHITE_KEYS unlit white
// Keys between groups. Black Keys are never used.
//
// GROUPS is also the single list of Settings for the serial `set`/`settings` commands and the
// debug log. Configured in app_conf.h.

// defined in main.cpp
okbl::Settings &currentSettings();
void onSettingsChanged(); // after any Setting is set: logs the Settings and shows a Preview
void onMenuOpen();        // clears live key state and stops Demo Mode
void onMenuClose();       // saves the Settings

namespace okbl {
    namespace menu {
        // what an Option's LED shows in the Settings Menu
        using Swatch = CRGB (*)(const Settings &s, uint32_t nowMs);

        struct Option {
            const char *name;
            Swatch swatch;
        };

        enum class GroupKind : uint8_t {
            Radio,  // a Setting: one Key per Option; pressing one selects it
            Action, // a Menu Action: one Key; pressing it closes the menu and runs the action
        };

        struct MenuGroup {
            const char *name;
            GroupKind kind;
            const Option *options;
            uint8_t optionCount;           // Action: 1
            uint8_t &(*value)(Settings &); // Radio only: the Setting's field as its underlying byte
            void (*action)();              // Action only
        };

        extern const MenuGroup GROUPS[];
        extern const size_t GROUP_COUNT;

        // case-insensitive; nullptr if there's no such group
        const MenuGroup *findGroup(const char *name);

        uint8_t valueOf(const MenuGroup &group, const Settings &s);
        // Radio only: writes the value, then calls onSettingsChanged()
        void setValue(const MenuGroup &group, uint8_t value);

        // e.g. "LightMode=PressedKey Color=PitchHue Brightness=Velocity"
        void printSettings(const Settings &s);

        // ---- Settings Menu state ----

        // works out the layout; call once at boot. Halts with a message if it doesn't fit the keyboard.
        void begin();

        void open();
        void close();
        void toggle(); // encoder press
        bool isOpen();

        // Only call while open. Returns false when the Key is above the menu: the menu closes
        // and the Key should be played as a normal note.
        bool onKeyDown(uint8_t keyIndex);

        // non-blocking; call every loop. Closes the menu after TIMEOUT_MS without a key press.
        void poll();

        // Fills target (NUM_LEDS long) with the menu: Swatches on Option Keys, black elsewhere.
        void render(CRGB *target, uint32_t nowMs);
    }
}

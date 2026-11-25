#pragma once

#include <Arduino.h>

#include "settings.h"

// Saves Settings to NVS (the flash key-value store) so they survive a power cycle, and loads them
// at boot.
//
// Each Setting is stored under its menu group's name, plus "BaseHue". A stored value that's
// missing or out of range keeps its default, so Settings can be added, or Options removed,
// without clearing the store. Renaming a group loses its saved value.
//
// Writes are rate-limited to save the flash: at most one write per okbl:::store::MIN_WRITE_INTERVAL_MS,
// with boot counting as a write. A save asked for too soon waits until the interval is up, and
// then writes whatever the Settings are by that time. Saving Settings that haven't changed since
// the last write doesn't touch the flash.

namespace kbl {
    namespace store {
        // Replaces s with the saved Settings, keeping defaults for anything not saved.
        void begin(Settings &s);

        // Asks for the current Settings to be saved. Does nothing more if a save is already waiting.
        void requestSave();

        // non-blocking; call every loop. Writes s once a requested save is due.
        void poll(const Settings &s);
    }
}

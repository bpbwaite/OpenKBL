#include "settings_store.h"

#include <Preferences.h>

#include "app_conf.h"
#include "settings_menu.h"

namespace okbl
{
    namespace store
    {
        namespace
        {
            const char *NAMESPACE    = "kbl-settings";
            const char *BASE_HUE_KEY = "BaseHue";

            Preferences prefs;
            Settings saved; // what the flash holds now
            bool savePending     = false;
            uint32_t lastWriteMs = 0; // boot counts as a write

            // a Setting changed since `saved`, by group
            bool changed(const menu::MenuGroup &group, const Settings &s) {
                return menu::valueOf(group, s) != menu::valueOf(group, saved);
            }

            bool anyChanged(const Settings &s) {
                for (size_t g = 0; g < menu::GROUP_COUNT; g++) {
                    const menu::MenuGroup &group = menu::GROUPS[g];
                    if (group.kind == menu::GroupKind::Radio && changed(group, s))
                        return true;
                }
                return s.baseHue != saved.baseHue;
            }

            // writes only the Settings that changed
            void write(const Settings &s) {
                if (!prefs.begin(NAMESPACE, false)) {
                    debugf("Settings: couldn't open storage to save\n");
                    return;
                }
                for (size_t g = 0; g < menu::GROUP_COUNT; g++) {
                    const menu::MenuGroup &group = menu::GROUPS[g];
                    if (group.kind == menu::GroupKind::Radio && changed(group, s))
                        prefs.putUChar(group.name, menu::valueOf(group, s));
                }
                if (s.baseHue != saved.baseHue)
                    prefs.putUChar(BASE_HUE_KEY, s.baseHue);
                prefs.end();
                saved = s;
                debugf("Settings: saved\n");
            }
        } // namespace

        void begin(Settings &s) {
            lastWriteMs = millis();
            // read-only open fails if nothing has been saved yet
            if (prefs.begin(NAMESPACE, true)) {
                for (size_t g = 0; g < menu::GROUP_COUNT; g++) {
                    const menu::MenuGroup &group = menu::GROUPS[g];
                    if (group.kind != menu::GroupKind::Radio || !prefs.isKey(group.name))
                        continue;
                    uint8_t value = prefs.getUChar(group.name);
                    if (value < group.optionCount)
                        group.value(s) = value;
                }
                if (prefs.isKey(BASE_HUE_KEY))
                    s.baseHue = prefs.getUChar(BASE_HUE_KEY);
                prefs.end();
                debugf("Settings: loaded\n");
            }
            else {
                debugf("Settings: none saved, using defaults\n");
            }
            saved = s;
        }

        void requestSave() {
            if (savePending)
                return;
            savePending           = true;
            uint32_t sinceWriteMs = millis() - lastWriteMs;
            if (sinceWriteMs < MIN_WRITE_INTERVAL_MS)
                debugf("Settings: save in %lu s\n", (unsigned long) (MIN_WRITE_INTERVAL_MS - sinceWriteMs + 999) / 1000);
        }

        void poll(const Settings &s) {
            if (!savePending || millis() - lastWriteMs < MIN_WRITE_INTERVAL_MS)
                return;
            savePending = false;
            if (!anyChanged(s))
                return; // nothing to write, so the flash isn't touched and the interval doesn't restart
            write(s);
            lastWriteMs = millis();
        }
    } // namespace store
} // namespace okbl

#include "settings_menu.h"

#include "app_conf.h"
#include "demo_mode.h"
#include "hardware.h"
#include "pitch_class.h"

namespace kbl {
    namespace menu {
        namespace {
            // ---- Swatches ----
            // One animated LED per Option. They only need to tell Options apart: pressing an
            // Option also shows a full-strip Preview of what it does.
            namespace swatch {
                // 0-255 sawtooth through each period
                uint8_t phase(uint32_t nowMs, uint32_t periodMs)
                {
                    return (nowMs % periodMs) * 256 / periodMs;
                }

                // a key strike: full at phase 0, dying away to nothing by phase 255
                uint8_t pluck(uint8_t phase)
                {
                    uint8_t left = 255 - phase;
                    return scale8(left, left);
                }

                // A Color as one animated LED. The Color Options show it directly; the others
                // animate in the current Color, so the menu previews the whole look.
                CRGB colorOf(Color color, uint32_t nowMs)
                {
                    switch (color)
                    {
                    case Color::PitchHue:
                        return CHSV(phase(nowMs, RAINBOW_PERIOD_MS), 255, 255);
                    case Color::KeyColors:
                        return CRGB(keycolors::PITCH_CLASS_COLORS[(nowMs / KEY_COLOR_STEP_MS) % 12]);
                    case Color::BaseHue:
                    default:
                        return CHSV(lights::BASE_HUE, 255, 255);
                    }
                }

                CRGB baseHue(const Settings &, uint32_t nowMs) { return colorOf(Color::BaseHue, nowMs); }
                CRGB pitchHue(const Settings &, uint32_t nowMs) { return colorOf(Color::PitchHue, nowMs); }
                CRGB keyColors(const Settings &, uint32_t nowMs) { return colorOf(Color::KeyColors, nowMs); }

                // ramps from dim to full, like playing harder and harder
                CRGB velocity(const Settings &s, uint32_t nowMs)
                {
                    return colorOf(s.color, nowMs).nscale8(max<uint8_t>(phase(nowMs, SWATCH_PERIOD_MS), 32));
                }

                CRGB full(const Settings &s, uint32_t nowMs) { return colorOf(s.color, nowMs); }

                // one key struck each period
                CRGB pressedKey(const Settings &s, uint32_t nowMs)
                {
                    return colorOf(s.color, nowMs).nscale8(pluck(phase(nowMs, SWATCH_PERIOD_MS)));
                }

                // three quick strikes then a rest, like one key echoing up the octaves
                CRGB allOctaves(const Settings &s, uint32_t nowMs)
                {
                    const uint32_t slotMs = SWATCH_PERIOD_MS / 4;
                    uint32_t t = nowMs % SWATCH_PERIOD_MS;
                    if (t >= 3 * slotMs)
                        return CRGB::Black;
                    return colorOf(s.color, nowMs).nscale8(pluck(phase(t, slotMs)));
                }

                // PedalFade Off: a strike that settles and stays lit, like a Sustained Key
                CRGB pedalHold(const Settings &s, uint32_t nowMs)
                {
                    uint8_t p = phase(nowMs, 2 * SWATCH_PERIOD_MS);
                    return colorOf(s.color, nowMs).nscale8(p < 40 ? 255 : 160);
                }

                // PedalFade On: a strike that holds, then dims out
                CRGB pedalFade(const Settings &s, uint32_t nowMs)
                {
                    const uint8_t HOLD = 40; // out of 255 of the period
                    uint8_t p = phase(nowMs, 2 * SWATCH_PERIOD_MS);
                    uint8_t left = p < HOLD ? 255 : 255 - (p - HOLD) * 255 / (255 - HOLD);
                    return colorOf(s.color, nowMs).nscale8(scale8(left, left));
                }

                // random twinkle in shifting colors
                CRGB demo(const Settings &, uint32_t nowMs)
                {
                    uint32_t step = nowMs / DEMO_TWINKLE_MS;
                    uint8_t level = (step * 2654435761u) >> 24; // cheap hash: a new random level each step
                    return CHSV(phase(nowMs, RAINBOW_PERIOD_MS), 200, max<uint8_t>(level, 32));
                }
            }

            // ---- Options: one per enum value, in enum order ----

            template <size_t N>
            constexpr uint8_t count(const Option (&)[N]) { return N; }

            const Option DEMO_OPTIONS[] = {
                {"Demo", swatch::demo},
            };

            const Option LIGHT_MODE_OPTIONS[] = {
                {"PressedKey", swatch::pressedKey},
                {"AllOctaves", swatch::allOctaves},
            };
            static_assert(count(LIGHT_MODE_OPTIONS) == (size_t)LightMode::COUNT, "one Option per LightMode");

            const Option COLOR_OPTIONS[] = {
                {"BaseHue", swatch::baseHue},
                {"PitchHue", swatch::pitchHue},
                {"KeyColors", swatch::keyColors},
            };
            static_assert(count(COLOR_OPTIONS) == (size_t)Color::COUNT, "one Option per Color");

            const Option BRIGHTNESS_OPTIONS[] = {
                {"Velocity", swatch::velocity},
                {"Full", swatch::full},
            };
            static_assert(count(BRIGHTNESS_OPTIONS) == (size_t)Brightness::COUNT, "one Option per Brightness");

            const Option PEDAL_FADE_OPTIONS[] = {
                {"Off", swatch::pedalHold},
                {"On", swatch::pedalFade},
            };
            static_assert(count(PEDAL_FADE_OPTIONS) == (size_t)PedalFade::COUNT, "one Option per PedalFade");
        }

        // Settings Menu layout order, from FIRST_KEY upward. Add new groups at the end so the
        // groups before them keep their Keys.
        const MenuGroup GROUPS[] = {
            {"Demo", GroupKind::Action, DEMO_OPTIONS, count(DEMO_OPTIONS), nullptr, demo::start},
            {"LightMode", GroupKind::Radio, LIGHT_MODE_OPTIONS, count(LIGHT_MODE_OPTIONS),
             [](Settings &s) -> uint8_t & { return reinterpret_cast<uint8_t &>(s.lightMode); }, nullptr},
            {"Color", GroupKind::Radio, COLOR_OPTIONS, count(COLOR_OPTIONS),
             [](Settings &s) -> uint8_t & { return reinterpret_cast<uint8_t &>(s.color); }, nullptr},
            {"Brightness", GroupKind::Radio, BRIGHTNESS_OPTIONS, count(BRIGHTNESS_OPTIONS),
             [](Settings &s) -> uint8_t & { return reinterpret_cast<uint8_t &>(s.brightness); }, nullptr},
            {"PedalFade", GroupKind::Radio, PEDAL_FADE_OPTIONS, count(PEDAL_FADE_OPTIONS),
             [](Settings &s) -> uint8_t & { return reinterpret_cast<uint8_t &>(s.pedalFade); }, nullptr},
        };
        const size_t GROUP_COUNT = sizeof(GROUPS) / sizeof(GROUPS[0]);

        const MenuGroup *findGroup(const char *name)
        {
            for (const MenuGroup &group : GROUPS)
            {
                if (strcasecmp(name, group.name) == 0)
                    return &group;
            }
            return nullptr;
        }

        uint8_t valueOf(const MenuGroup &group, const Settings &s)
        {
            return group.value(const_cast<Settings &>(s));
        }

        void setValue(const MenuGroup &group, uint8_t value)
        {
            group.value(currentSettings()) = value;
            onSettingsChanged();
        }

        void printSettings(const Settings &s)
        {
            bool first = true;
            for (const MenuGroup &group : GROUPS)
            {
                if (group.kind == GroupKind::Action)
                    continue;
                debugf("%s%s=%s", first ? "" : " ", group.name, group.options[valueOf(group, s)].name);
                first = false;
            }
            debugf("\n");
        }

        // ---- Settings Menu state ----

        namespace {
            const int8_t NONE = -1;
            int8_t keyGroup[leds::NUM_LEDS];  // GROUPS index for each Key, NONE for gaps and unused Keys
            int8_t keyOption[leds::NUM_LEDS]; // Option index within that group
            uint8_t lastMenuKey = 0;          // Keys above this are outside the menu

            bool menuOpen = false;
            uint32_t lastInputMs = 0;

            bool isSelected(const MenuGroup &group, uint8_t option, const Settings &s)
            {
                return group.kind == GroupKind::Action || valueOf(group, s) == option;
            }

            // the first white key at or above `key`
            uint32_t nextWhiteKey(uint32_t key)
            {
                while (key < leds::NUM_LEDS && !pitch::isWhiteKey(key))
                    key++;
                return key;
            }

            [[noreturn]] void haltLayout(const char *problem)
            {
                while (true)
                {
                    debugf("Settings Menu layout error: %s (see kbl::menu in app_conf.h)\n", problem);
                    delay(2000);
                }
            }
        }

        void begin()
        {
            for (uint8_t i = 0; i < leds::NUM_LEDS; i++)
            {
                keyGroup[i] = NONE;
                keyOption[i] = NONE;
            }
            if (!pitch::isWhiteKey(FIRST_KEY))
                haltLayout("FIRST_KEY must be a white key");

            // Options go on white keys only, with GAP_WHITE_KEYS white keys between groups, so
            // every Setting can be reached without the sharps and flats and the groups are evenly spaced.
            uint32_t key = FIRST_KEY;
            for (size_t g = 0; g < GROUP_COUNT; g++)
            {
                for (uint8_t o = 0; o < GROUPS[g].optionCount; o++)
                {
                    // the next white key, or past the gap when this starts a new group
                    if (g > 0 || o > 0)
                    {
                        uint8_t steps = o == 0 ? GAP_WHITE_KEYS + 1 : 1;
                        for (uint8_t step = 0; step < steps; step++)
                            key = nextWhiteKey(key + 1);
                    }
                    if (key >= leds::NUM_LEDS)
                        haltLayout("the groups run past the top of the keyboard");
                    keyGroup[key] = g;
                    keyOption[key] = o;
                }
            }
            lastMenuKey = key;
            debugf("Settings Menu: keys %d-%d\n", (int)FIRST_KEY, (int)lastMenuKey);
        }

        void open()
        {
            if (menuOpen)
                return;
            onMenuOpen();
            menuOpen = true;
            lastInputMs = millis();
            debugf("Settings Menu: open\n");
        }

        void close()
        {
            if (!menuOpen)
                return;
            menuOpen = false;
            debugf("Settings Menu: closed\n");
        }

        void toggle()
        {
            menuOpen ? close() : open();
        }

        bool isOpen()
        {
            return menuOpen;
        }

        bool onKeyDown(uint8_t keyIndex)
        {
            if (keyIndex > lastMenuKey)
            {
                close();
                return false;
            }
            lastInputMs = millis();
            if (keyGroup[keyIndex] == NONE)
                return true; // a gap, a black key, or below FIRST_KEY: ignored so a near miss doesn't close the menu

            const MenuGroup &group = GROUPS[keyGroup[keyIndex]];
            if (group.kind == GroupKind::Action)
            {
                close();
                group.action();
            }
            else
            {
                setValue(group, keyOption[keyIndex]);
            }
            return true;
        }

        void poll()
        {
            if (menuOpen && millis() - lastInputMs >= TIMEOUT_MS)
            {
                debugf("Settings Menu: timed out\n");
                close();
            }
        }

        void render(CRGB *target, uint32_t nowMs)
        {
            const Settings &s = currentSettings();
            for (uint8_t i = 0; i < leds::NUM_LEDS; i++)
            {
                if (keyGroup[i] == NONE)
                {
                    target[i] = CRGB::Black;
                    continue;
                }
                const MenuGroup &group = GROUPS[keyGroup[i]];
                const uint8_t option = keyOption[i];
                target[i] = group.options[option].swatch(s, nowMs);
                if (!isSelected(group, option, s))
                {
                    target[i].nscale8(UNSELECTED_SCALE);
                }
            }
        }
    }
}

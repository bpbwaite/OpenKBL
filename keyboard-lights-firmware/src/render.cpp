#include "render.h"

#include "app_conf.h"
#include "hardware.h"
#include "pitch_class.h"

namespace kbl {
    namespace render {
        namespace {
            using pitch::keyPitchClass;

            // What the Light Mode decides for one LED.
            struct Lit {
                uint8_t velocity; // how hard it lights; 0 = unlit
                bool fading;      // under Pedal Fade: Sustained, not held
                uint32_t fadeMs;  // how long it has been fading
                hands::Hand hand; // for Color::Hands
                bool corrected;   // its hand was just corrected (KBL_HAND_DEBUG)
            };

            // A Key's Lit, as if it were the only Key lighting its LED.
            Lit keyLit(const KeyState &key, const Settings &s, uint32_t nowMs)
            {
                if (!isSounding(key))
                    return {};
                bool fading = !key.held && s.pedalFade == PedalFade::On;
                bool corrected = false;
#ifdef KBL_HAND_DEBUG
                corrected = key.correctedMs && nowMs - key.correctedMs < hands::CORRECTED_FLASH_MS;
#endif
                return {key.velocity, fading, fading ? nowMs - key.releasedMs : 0, key.hand, corrected};
            }

            // ---- Light Mode: which LEDs light, and how hard ----

            void lightPressedKey(Lit *lit, const KeyState *keys, const Settings &s, uint32_t nowMs)
            {
                for (uint8_t i = 0; i < leds::NUM_LEDS; i++)
                {
                    lit[i] = keyLit(keys[i], s, nowMs);
                }
            }

            // Each Sounding PitchClass lights all of its keys, at the hardest velocity among that
            // PitchClass's keys. It only fades while none of them is held, and then fades from the
            // most recent release.
            void lightAllOctaves(Lit *lit, const KeyState *keys, const Settings &s, uint32_t nowMs)
            {
                Lit byClass[12] = {};
                for (uint8_t i = 0; i < leds::NUM_LEDS; i++)
                {
                    Lit k = keyLit(keys[i], s, nowMs);
                    if (!k.velocity)
                        continue;
                    Lit &c = byClass[keyPitchClass(i)];
                    if (!c.velocity)
                    {
                        c = k;
                        continue;
                    }
                    c.velocity = max(c.velocity, k.velocity);
                    c.fading = c.fading && k.fading;
                    c.fadeMs = c.fading ? min(c.fadeMs, k.fadeMs) : 0;
                }
                for (uint8_t i = 0; i < leds::NUM_LEDS; i++)
                {
                    lit[i] = byClass[keyPitchClass(i)];
                }
            }

            // ---- Pedal Fade ----
            // A fading LED loses brightness at one fixed rate, in halvings per second, so it looks
            // like an even fade to the eye (a linear fade seems to barely change, then drop off at
            // the end). The rate is set so that the LED's color at full brightness would reach
            // floorPeak, the dimmest target that still shows, after SUSTAIN_FADE_MS: a full-velocity
            // key takes that long in any Color. A dimmer LED (a softer key, under
            // Brightness::Velocity) starts closer to the floor, so it goes dark sooner. On reaching
            // the floor it turns off, and the normal Fading's quick tail below the floor takes it out.

            // color after fadeMs of Pedal Fade; black once it reaches the floor. fullPeak is the
            // largest channel of this LED's color at full brightness.
            CRGB fade(CRGB color, uint8_t fullPeak, uint32_t fadeMs, uint8_t floorPeak)
            {
                // At the lowest Master Brightness the floor is near full, which would leave no room
                // to fade at all: then time it as if the floor were at most half brightness.
                // raiseToFloor() keeps the LED at the floor, and softer keys still go dark sooner.
                uint8_t timingFloor = constrain(floorPeak, 1, 127);
                // halvings from full brightness down to the floor, in 1/256ths
                uint32_t fullHalvings = lroundf(log2f((float)max<uint8_t>(fullPeak, timingFloor + 1) / timingFloor) * 256);
                uint32_t halvings = (uint64_t)min(fadeMs, 16 * lights::SUSTAIN_FADE_MS) * fullHalvings / lights::SUSTAIN_FADE_MS;
                if ((halvings >> 8) >= 16)
                    return CRGB::Black;
                // 2^-halvings in 8.8 fixed point: whole halvings shift, and the fractional part is
                // close enough to linear between them (within 6%)
                uint32_t level = 0xFFFFu >> (halvings >> 8);
                level -= level * (halvings & 0xFF) / 512;
                // a key that starts at or near the floor (soft, or at low Master Brightness) still
                // stays lit for SUSTAIN_FADE_MIN_MS; raiseToFloor() keeps it visible meanwhile
                uint8_t peak = max(color.r, max(color.g, color.b));
                if ((peak * level >> 16) <= timingFloor && fadeMs >= lights::SUSTAIN_FADE_MIN_MS)
                    return CRGB::Black;
                // _video scaling never takes a lit channel to zero, so the hue holds to the end
                return color.nscale8_video(level >> 8);
            }

            // ---- shading: Brightness, then Color ----

            uint8_t brightnessOf(uint8_t velocity, const Settings &s)
            {
                switch (s.brightness)
                {
                case Brightness::Full:
                    return 255;
                case Brightness::Velocity:
                default:
                    return map(velocity, 1, 127, leds::MIN_PEAK, 255);
                }
            }

            // Hue-based colors take the brightness as the CHSV value, so they keep FastLED's
            // dimming curve; fixed RGB colors are scaled linearly.
            CRGB colorOf(uint8_t ledIndex, uint8_t brightness, const Lit &lit, const Settings &s)
            {
                switch (s.color)
                {
                case Color::Hands:
                    if (lit.corrected)
                        return CRGB(hands::CORRECTED_COLOR).nscale8(brightness);
                    return CRGB(lit.hand == hands::Hand::Left ? hands::LEFT_COLOR : hands::RIGHT_COLOR).nscale8(brightness);
                case Color::PitchHue:
                    return CHSV(ledIndex * 255 / (leds::NUM_LEDS - 1), 255, brightness);
                case Color::KeyColors:
                    return CRGB(keycolors::PITCH_CLASS_COLORS[keyPitchClass(ledIndex)]).nscale8(brightness);
                case Color::BaseHue:
                default:
                    return CHSV(lights::BASE_HUE, 255, brightness);
                }
            }
        }

        void computeTargets(CRGB *target, const KeyState *keys, const Settings &s, uint32_t nowMs, uint8_t floorPeak)
        {
            Lit lit[leds::NUM_LEDS];
            switch (s.lightMode)
            {
            case LightMode::AllOctaves:
                lightAllOctaves(lit, keys, s, nowMs);
                break;
            case LightMode::PressedKey:
            default:
                lightPressedKey(lit, keys, s, nowMs);
                break;
            }

            // Pedal Fade works on the finished color, so it fades at the same rate under every
            // Brightness and Color (hue-based colors would otherwise add FastLED's dimming curve).
            for (uint8_t i = 0; i < leds::NUM_LEDS; i++)
            {
                if (!lit[i].velocity)
                {
                    target[i] = CRGB::Black;
                    continue;
                }
                target[i] = colorOf(i, brightnessOf(lit[i].velocity, s), lit[i], s);
                if (lit[i].fading)
                {
                    CRGB full = colorOf(i, 255, lit[i], s);
                    target[i] = fade(target[i], max(full.r, max(full.g, full.b)), lit[i].fadeMs, floorPeak);
                }
            }
        }

        void previewKeys(KeyState *keys, const Settings &s)
        {
            for (uint8_t i = 0; i < leds::NUM_LEDS; i++)
            {
                keys[i] = {};
                keys[i].velocity = map(i, 0, leds::NUM_LEDS - 1, 1, 127); // shows off Brightness::Velocity
                keys[i].hand = hands::bySplitPoint(i);                    // shows off Color::Hands
            }

            switch (s.lightMode)
            {
            case LightMode::AllOctaves:
            {
                // one C major chord at middle C; the Light Mode spreads it across the keyboard
                const uint8_t C4 = 60 - kbl::keys::MIDI_NOTE_LOWEST;
                for (uint8_t i : {C4, uint8_t(C4 + 4), uint8_t(C4 + 7)})
                {
                    keys[i].held = true;
                    keys[i].velocity = 127;
                }
                break;
            }
            case LightMode::PressedKey:
            default:
                for (uint8_t i = 0; i < leds::NUM_LEDS; i++)
                {
                    keys[i].held = true;
                }
                break;
            }
        }
    }
}

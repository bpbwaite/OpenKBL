#include "demo_mode.h"

#include <FastLED.h>

#include "app_conf.h"
#include "hardware.h"

namespace kbl {
    namespace demo {
        namespace {
            KeyState demoKeys[leds::NUM_LEDS];
            uint32_t keyStartMs[leds::NUM_LEDS];
            bool isActive = false;
            uint32_t lastActivityMs = 0;

            void updateKeys()
            {
                for (uint8_t i = 0; i < leds::NUM_LEDS; i++)
                {
                    if (demoKeys[i].held && millis() - keyStartMs[i] >= KEY_MS)
                    {
                        demoKeys[i].held = false;
                    }
                }
                EVERY_N_MILLISECONDS(ROLL_MS)
                {
                    uint32_t roll = random(ROLL_MAX + 1);
                    if (roll < leds::NUM_LEDS)
                    {
                        demoKeys[roll].held = true;
                        demoKeys[roll].velocity = random(1, 128);
                        demoKeys[roll].hand = hands::bySplitPoint(roll);
                        keyStartMs[roll] = millis();
                    }
                }
            }
        }

        void start()
        {
            if (isActive)
                return;
            onDemoModeStart();
            isActive = true;
            debugf("Demo mode: on\n");
        }

        void stop()
        {
            if (!isActive)
                return;
            isActive = false;
            for (KeyState &key : demoKeys)
            {
                key = {};
            }
            debugf("Demo mode: off\n");
        }

        bool active()
        {
            return isActive;
        }

        void recordActivity()
        {
            lastActivityMs = millis();
        }

        void poll()
        {
            if (!isActive && millis() - lastActivityMs >= IDLE_MS)
            {
                start();
            }
            if (isActive)
            {
                updateKeys();
            }
        }

        const KeyState *keys()
        {
            return demoKeys;
        }
    }
}

// Brightness floor test. Build and flash with: pio run -e brightness_floor -t upload
//
// Every CYCLE_MS, every LED gets its own new random color (random hue and saturation).
// Each key has a fixed brightness, rising by one per key from A0. Brightness here is the
// largest of the R, G and B values reaching the LED; the other two keep the color's
// proportions. Color correction, power limiting and sag compensation are all off.
//
// Press the encoder button to toggle dithering:
// - off (boot): global brightness 255, A0 = 1, C8 = 88, exactly what's sent to the LED.
// - on: values are sent doubled with global brightness 127 (exactly half), so FastLED's
//   temporal dithering shows the half steps: A0 = 0.5, C8 = 87.5.
//
// Watch for keys whose color looks wrong or flickers; the highest such key gives the
// brightness floor (key-to-brightness table printed over serial at boot).
//
// No power limit: run it on the PSU, not USB power alone.

#include <Arduino.h>
#include <FastLED.h>

#include "app_conf.h"
#include "hardware.h"
#include "rotary_encoder.h"

namespace {
    const uint32_t CYCLE_MS = 1000;
    const uint8_t DITHER_GLOBAL_BRIGHTNESS = 127; // FastLED scales by (127 + 1) / 256 = exactly half

    CRGB leds[kbl::leds::NUM_LEDS];
    CRGB fullColors[kbl::leds::NUM_LEDS]; // each LED's color at full brightness
    bool dithering = false;

    // largest channel value put in the LED buffer for a key, before global brightness
    uint8_t keyPeak(uint8_t keyIndex)
    {
        return dithering ? 2 * keyIndex + 1 : keyIndex + 1;
    }

    // scientific pitch name of a key, e.g. "C#4"
    void keyName(uint8_t keyIndex, char *out)
    {
        static const char *NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        uint8_t note = keyIndex + kbl::keys::MIDI_NOTE_LOWEST;
        sprintf(out, "%s%d", NAMES[note % 12], note / 12 - 1);
    }

    // color with its largest channel scaled to exactly `peak`
    CRGB scaleToPeak(CRGB color, uint8_t peak)
    {
        uint8_t colorPeak = max(color.r, max(color.g, color.b));
        if (colorPeak == 0)
            return CRGB::Black;
        return CRGB((color.r * peak + colorPeak / 2) / colorPeak,
                    (color.g * peak + colorPeak / 2) / colorPeak,
                    (color.b * peak + colorPeak / 2) / colorPeak);
    }

    void fillLEDs()
    {
        for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++)
        {
            leds[i] = scaleToPeak(fullColors[i], keyPeak(i));
        }
    }

    void newColors()
    {
        for (CRGB &color : fullColors)
        {
            color = CHSV(random(256), random(256), 255);
        }
        fillLEDs();
    }

    void applyDithering()
    {
        FastLED.setDither(dithering ? BINARY_DITHER : DISABLE_DITHER);
        FastLED.setBrightness(dithering ? DITHER_GLOBAL_BRIGHTNESS : 255);
        fillLEDs();
        debugf("Dithering %s: A0 = %s, C8 = %s\n", dithering ? "ON" : "OFF",
               dithering ? "0.5" : "1", dithering ? "87.5" : "88");
    }

    void printKeyTable()
    {
        debugf("Brightness floor test: brightness = largest RGB value reaching the LED\n");
        debugf("key=brightness with dithering off (0.5 lower with dithering on)\n");
        char name[5];
        for (uint8_t i = 0; i < kbl::leds::NUM_LEDS; i++)
        {
            keyName(i, name);
            debugf("%s=%d%s", name, i + 1, (i % 12 == 11) ? "\n" : "  ");
        }
        debugf("\nPress the encoder to toggle dithering.\n");
    }
}

void setup()
{
    debugStart();
    debugWaitForInterface();

    digitalWrite(kbl::pins::ENABLE_PSU, HIGH);
    rotaryBegin();

    FastLED.addLeds<WS2812B, kbl::pins::RGB_DATA, EOrder::GRB>(leds, kbl::leds::NUM_LEDS);
    FastLED.setCorrection(UncorrectedColor);
    FastLED.setTemperature(UncorrectedTemperature);

    randomSeed(esp_random());
    printKeyTable();
    newColors();
    applyDithering();
}

void loop()
{
    if (rotaryPoll().presses % 2) // each press toggles
    {
        dithering = !dithering;
        applyDithering();
    }

    EVERY_N_MILLISECONDS(CYCLE_MS)
    {
        newColors();
    }
    // re-send at the normal frame rate, like the real firmware does; dithering needs it too
    EVERY_N_MILLISECONDS(kbl::lights::FRAME_MS)
    {
        FastLED.show();
    }
}

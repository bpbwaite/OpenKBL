#pragma once

#include <Arduino.h>

// These values are set in stone by the PCB design and the piano. Don't modify them.
// Tunable behaviour lives in app_conf.h.

#ifndef PCB_REVISION
#error "Set -DPCB_REVISION in platformio.ini"
#endif

namespace okbl
{
    namespace pins
    {
        static const uint8_t RGB_DATA   = GPIO_NUM_3;  // D1 (A1)
        static const uint8_t MIDI_RX    = GPIO_NUM_5;  // D3
        static const uint8_t ENABLE_PSU = GPIO_NUM_10; // D10
        static const uint8_t ROTARY_SW  = GPIO_NUM_6;  // D4
        static const uint8_t ROTARY_A   = GPIO_NUM_9;  // D9
        static const uint8_t ROTARY_B   = GPIO_NUM_8;  // D8
        // revision 0 has no pullup resistor on the rotary switch
        static const uint8_t ROTARY_SW_PIN_MODE = PCB_REVISION == 0 ? INPUT_PULLUP : INPUT;

        static const uint8_t RESERVED = GPIO_NUM_2; // D0 (A0)
    } // namespace pins
    namespace rotary
    {
        static const int8_t CLOCKWISE            = 1; // flip the sign if turning clockwise dims the LEDs
        static const uint32_t SWITCH_DEBOUNCE_MS = 20;
        // the PCB's capacitors filter contact bounce on the A/B lines, so they need no debounce
    } // namespace rotary
    namespace leds
    {
        static const uint32_t NUM_LEDS = 88;
        static const float PSU_VOLTAGE = 5.0;
        static const float PSU_MAX_MA  = 4000.0; // derated to 80% of 5A
        static const float USB_MAX_MA  = 500.0;  // budget when the board may be powered from USB

        // Measured with the brightness_floor test: below this, an LED flickers or shows the
        // wrong color. It's the lowest the largest of R, G and B may be when it reaches the strip,
        // after every scaling step. F#1 (10, or 9.5 dithered) was the highest key that flickered.
        static const uint8_t MIN_PEAK = 11;

        // napkin math: fet is probably not limited by power dissipation
        // with R_ds_on at <6 mOhm at 3.3 V, we dissipate 30 mW from 5A
        // worst case 160C/W -> ~5C rise at 25C
        // if the numbers come back different we'll put a note here
    } // namespace leds
    namespace keys
    {
        // 88-key piano: LED i lights the key at MIDI note MIDI_NOTE_LOWEST + i
        static const uint8_t MIDI_NOTE_LOWEST  = 21;                                    // A0, maps to LED 0
        static const uint8_t MIDI_NOTE_HIGHEST = MIDI_NOTE_LOWEST + leds::NUM_LEDS - 1; // C8
    } // namespace keys
} // namespace okbl

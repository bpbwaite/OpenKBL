#pragma once

#include <Arduino.h>

// These values are set in stone due to the PCB design. Don't modify them.

#ifndef PCB_REVISION
#error "Set -DPCB_REVISION in platformio.ini"
#endif

namespace kbl {
    namespace pins {
        static const uint8_t RGB_DATA = GPIO_NUM_3; // D1 (A1)
        static const uint8_t MIDI_RX = GPIO_NUM_5; // D3
        static const uint8_t ENABLE_PSU = GPIO_NUM_10; // D10
        static const uint8_t ROTARY_SW = GPIO_NUM_6; // D4
        static const uint8_t ROTARY_A = GPIO_NUM_9; // D9
        static const uint8_t ROTARY_B = GPIO_NUM_8; // D8
        // revision 0 has no pullup resistor on the rotary switch
        static const uint8_t ROTARY_SW_PIN_MODE = PCB_REVISION == 0 ? INPUT_PULLUP : INPUT;

        static const uint8_t RESERVED = GPIO_NUM_2; // D0 (A0)

    }
    namespace leds {
        static const uint32_t NUM_LEDS = 88;
        static const float PSU_VOLTAGE = 5.0;
        static const float PSU_MAX_MA = 4000.0; // derated to 80% of 5A
        static const float LED_DRAW_MA = 60.0;
        static const float LED_DRAW_COMBINED = leds::NUM_LEDS * LED_DRAW_MA;

        // PER-BOARD (exception to the note above): supply voltage measured at the far end
        // of the strip (last LED) at full power. Used to even out brightness along the strip.
        static const float FAR_END_VOLTAGE = 4.92;

        // napkin math: fet is probably not limited by power dissipation
        // with R_ds_on at <6 mOhm at 3.3 V, we dissipate 30 mW from 5A
        // worst case 160C/W -> ~5C rise at 25C
        // if the numbers come back different we'll put a note here
    }
}
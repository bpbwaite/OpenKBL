#pragma once

#include <Arduino.h>

#include "hardware.h"

// Rotary encoder polling. The encoder rests with A and B both high, and the PCB's
// capacitors filter contact bounce on the encoder lines.

// defined in main.cpp
void onRotaryTurn(int steps);
void onRotaryPress();

namespace kbl {
    namespace rotary {
        static const int8_t CLOCKWISE = 1; // flip the sign if turning clockwise dims the LEDs
        static const uint32_t SWITCH_DEBOUNCE_MS = 20;

        // quadrature step for each (previous A,B << 2 | current A,B)
        static const int8_t TRANSITIONS[16] = {
            0, -1, 1, 0,
            1, 0, 0, -1,
            -1, 0, 0, 1,
            0, 1, -1, 0
        };
    }
}

inline void rotaryBegin() {
    pinMode(kbl::pins::ROTARY_A, INPUT);
    pinMode(kbl::pins::ROTARY_B, INPUT);
    pinMode(kbl::pins::ROTARY_SW, kbl::pins::ROTARY_SW_PIN_MODE);
}

// non-blocking; call every loop
inline void rotaryPoll() {
    static uint8_t encoderState = 0b11;
    static int8_t encoderSteps = 0;
    static bool switchDown = false;
    static uint32_t switchChangedMs = 0;

    uint8_t current = digitalRead(kbl::pins::ROTARY_A) << 1 | digitalRead(kbl::pins::ROTARY_B);
    if (current != encoderState) {
        encoderSteps += kbl::rotary::TRANSITIONS[encoderState << 2 | current];
        encoderState = current;
        if (current == 0b11) { // back at rest: one detent, if most steps went one way
            if (encoderSteps >= 2) onRotaryTurn(kbl::rotary::CLOCKWISE);
            else if (encoderSteps <= -2) onRotaryTurn(-kbl::rotary::CLOCKWISE);
            encoderSteps = 0;
        }
    }

    bool down = digitalRead(kbl::pins::ROTARY_SW) == LOW;
    if (down != switchDown && millis() - switchChangedMs >= kbl::rotary::SWITCH_DEBOUNCE_MS) {
        switchDown = down;
        switchChangedMs = millis();
        if (down) onRotaryPress();
    }
}

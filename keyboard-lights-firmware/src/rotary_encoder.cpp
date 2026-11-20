#include "rotary_encoder.h"

#include <soc/gpio_reg.h>

#include "hardware.h"

// Everything the interrupt handlers touch lives in IRAM (code) or DRAM (data), so they
// still run while the flash cache is off, as it is during an OTA write. The pins are read
// straight from the GPIO input register: digitalRead() isn't safe to call there.
//
// The handlers only ever increment the totals. The C3 has a single core and reads an
// aligned 32-bit word in one instruction, so loop() can read a total without locking and
// work out what changed from the value it saw last time.

namespace {
    // quadrature step for each (previous A,B << 2 | current A,B)
    DRAM_ATTR const int8_t TRANSITIONS[16] = {
        0, -1, 1, 0,
        1, 0, 0, -1,
        -1, 0, 0, 1,
        0, 1, -1, 0
    };

    const uint32_t PIN_A_MASK = 1u << kbl::pins::ROTARY_A;
    const uint32_t PIN_B_MASK = 1u << kbl::pins::ROTARY_B;
    const uint32_t PIN_SW_MASK = 1u << kbl::pins::ROTARY_SW;

    // written only by the handlers
    volatile int32_t detentTotal = 0;
    volatile uint32_t pressTotal = 0;
    uint8_t encoderState = 0b11; // A,B; the encoder rests with both high
    int8_t encoderSteps = 0;     // quadrature steps since the encoder last rested
    bool switchSettledUp = true; // switch level after its last edge
    uint32_t switchEdgeMs = 0;

    // read only by loop()
    int32_t detentsSeen = 0;
    uint32_t pressesSeen = 0;

    inline uint32_t IRAM_ATTR readPins() {
        return REG_READ(GPIO_IN_REG);
    }

    // A and B share one handler; reading both at once keeps the pair consistent.
    void IRAM_ATTR onEncoderEdge() {
        uint32_t pins = readPins();
        uint8_t current = ((pins & PIN_A_MASK) ? 0b10 : 0) | ((pins & PIN_B_MASK) ? 0b01 : 0);
        if (current == encoderState)
            return;
        encoderSteps += TRANSITIONS[encoderState << 2 | current];
        encoderState = current;
        if (current == 0b11) { // back at rest: one detent, if most steps went one way
            if (encoderSteps >= 2) detentTotal = detentTotal + 1;
            else if (encoderSteps <= -2) detentTotal = detentTotal - 1;
            encoderSteps = 0;
        }
    }

    // A press is the first edge after the switch has sat released for SWITCH_DEBOUNCE_MS.
    // That decision uses the level seen after the previous edge, which has settled by now,
    // rather than this edge's level, which bounce may already have flipped back.
    void IRAM_ATTR onSwitchEdge() {
        uint32_t now = millis();
        if (switchSettledUp && now - switchEdgeMs >= kbl::rotary::SWITCH_DEBOUNCE_MS) {
            pressTotal = pressTotal + 1;
        }
        switchSettledUp = readPins() & PIN_SW_MASK;
        switchEdgeMs = now;
    }
}

void rotaryBegin() {
    pinMode(kbl::pins::ROTARY_A, INPUT);
    pinMode(kbl::pins::ROTARY_B, INPUT);
    pinMode(kbl::pins::ROTARY_SW, kbl::pins::ROTARY_SW_PIN_MODE);

    uint32_t pins = readPins();
    encoderState = ((pins & PIN_A_MASK) ? 0b10 : 0) | ((pins & PIN_B_MASK) ? 0b01 : 0);
    switchSettledUp = pins & PIN_SW_MASK;
    switchEdgeMs = millis() - kbl::rotary::SWITCH_DEBOUNCE_MS;

    attachInterrupt(kbl::pins::ROTARY_A, onEncoderEdge, CHANGE);
    attachInterrupt(kbl::pins::ROTARY_B, onEncoderEdge, CHANGE);
    attachInterrupt(kbl::pins::ROTARY_SW, onSwitchEdge, CHANGE);
}

RotaryEvents rotaryPoll() {
    int32_t detents = detentTotal;
    uint32_t presses = pressTotal;
    RotaryEvents events = {
        (int32_t)((uint32_t)detents - (uint32_t)detentsSeen) * kbl::rotary::CLOCKWISE,
        presses - pressesSeen,
    };
    detentsSeen = detents;
    pressesSeen = presses;
    return events;
}

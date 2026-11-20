#pragma once

#include <Arduino.h>

// Rotary encoder, interrupt driven. Pin-change interrupts on A, B and the switch keep
// running totals, so turns and presses aren't lost while loop() is busy (e.g. an OTA
// flash write). loop() collects them with rotaryPoll(). Direction and switch debounce
// are in hardware.h.

struct RotaryEvents {
    int32_t steps;    // detents turned since the last poll; positive = brighter
    uint32_t presses; // switch presses since the last poll
};

void rotaryBegin();

// non-blocking and interrupt safe; call every loop. Returns what happened since the last call.
RotaryEvents rotaryPoll();

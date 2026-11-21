#pragma once

#include <Arduino.h>

#include "hands.h"

struct KeyState
{
    bool held;           // key is physically down
    bool sustained;      // key was released while the sustain pedal was down
    uint8_t velocity;
    uint32_t releasedMs; // millis() when the key was last released, for Pedal Fade
    kbl::hands::Hand hand; // which hand pressed it, for Color::Hands
    uint32_t correctedMs;  // millis() when its hand was last corrected; 0 = never (KBL_HAND_DEBUG)
};

inline bool isSounding(const KeyState &key)
{
    return key.held || key.sustained;
}

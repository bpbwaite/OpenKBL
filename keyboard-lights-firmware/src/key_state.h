#pragma once

#include <Arduino.h>

#include "hands.h"

struct KeyState
{
    bool held;           // key is physically down
    bool sustained;      // key was released while the sustain pedal was down
    uint8_t velocity;
    uint32_t releasedMs; // millis() when the key was last released, for Pedal Fade
    bool blanking;       // Blank On Press: its LED was lit when pressed, so it's held dark first
    uint32_t pressedMs;  // millis() when the key was last pressed, for Blank On Press
    kbl::hands::Hand hand; // which hand pressed it, for Color::Hands
    uint32_t correctedMs;  // millis() when its hand was last corrected; 0 = never (KBL_HAND_DEBUG)
};

inline bool isSounding(const KeyState &key)
{
    return key.held || key.sustained;
}

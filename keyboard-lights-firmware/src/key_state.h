#pragma once

#include <Arduino.h>

struct KeyState
{
    bool held;           // key is physically down
    bool sustained;      // key was released while the sustain pedal was down
    uint8_t velocity;
    uint32_t releasedMs; // millis() when the key was last released, for Pedal Fade
};

inline bool isSounding(const KeyState &key)
{
    return key.held || key.sustained;
}

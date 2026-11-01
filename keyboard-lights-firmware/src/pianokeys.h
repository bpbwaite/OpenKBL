#pragma once

#include <Arduino.h>
#include "app_conf.h"

#define NUM_KEYS (88)

typedef struct Key {
    int index;
    char name1; // for white keys, its A,B,C. black keys: which key is sharp?
    char name2; // for white keys == name1. black keys: which key is flat?
    // packing?
    int canonicalInstance; // start at A0 = 0, increment each octave
} Key;

void printKey(Key k) {
    char buf[] = "D#3/Eb3";

    if (k.name1 == k.name2) {
        snprintf(buf, sizeof(buf), "%c%d", k.name1, k.canonicalInstance);
    } else {
        snprintf(buf, sizeof(buf), "%c%d/%c%d", k.name1, k.canonicalInstance, k.name2, k.canonicalInstance);
    }

    debugf("%s (idx=%d)\n", buf, k.index);
}


// keys:
// index, key sharp name, key flat name, key instance
Key getKey(int index) {
    if (index < 0 || index > NUM_KEYS - 1) {
        return Key({-1, 'X', 'X', -1});
    }

    static const char names[][2] = {
        {'A', 'A'}, {'A', 'B'}, {'B', 'B'}, {'C', 'C'},
        {'C', 'D'}, {'D', 'D'}, {'D', 'E'}, {'E', 'E'},
        {'F', 'F'}, {'F', 'G'}, {'G', 'G'}, {'G', 'A'}
    };
    const int keyInOctave = index % 12;
    return Key({index, names[keyInOctave][0], names[keyInOctave][1], index / 12});
#pragma once

#include <stdint.h>

// Works out which hand is playing each Held Key, from the order of key presses alone (never their
// timing). Plain C++ with no Arduino dependencies, so it can be tested on the PC
// (test/test_hands). Keys are key indices, 0 = the lowest key on the piano.
//
// Each hand has an anchor: the range of its Held Keys right after the last press it was given.
// Releases don't move it. Until a hand has played, the Split Point stands in for its anchor. If
// every Key comes up with the hands crossed, the two anchors swap.
//
// Fingers aren't tracked, but each hand's Held Keys say what they can: the thumb is on the hand's
// inner end (the right hand's lowest Key, the left hand's highest), and only the thumb stretches
// far. Fingers 2-5 cover at most fingerSpan, and hold at most four Keys. The thumb can take two
// Keys a step apart, which is how one hand holds six.
//
// On every press the tracker scores a few ways to split all Held Keys between the hands and keeps
// the cheapest: keep everything and give the new Key to either hand, or cut the sorted Held Keys
// at any point. A cut moves Keys to the other hand, correcting an earlier guess. It may only move
// Keys that were close calls, unless nothing else fits at all.

namespace okbl {
    namespace hands {
        enum class Hand : uint8_t {
            Left,
            Right,
        };

        // Costs are in half-semitones of distance a hand moves.
        struct Config {
            uint8_t splitKey;      // lowest Key a fresh right hand takes; below it, the left
            uint8_t maxSpan;       // semitones a hand reaches, thumb included, lowest to highest Key
            uint8_t fingerSpan;    // semitones fingers 2-5 reach, the thumb's Key(s) left out
            uint8_t thumbPairSpan; // most semitones between two Keys the thumb holds together
            uint16_t fingerOverCost; // each semitone fingers 2-5 would stretch past fingerSpan
            uint16_t thumbPairCost;  // the thumb holding two Keys
            uint16_t crossCost;      // highest left-hand Key above the lowest right-hand one
            uint16_t idleCrossCost;  // a hand's Keys past where the other, empty hand last was
            uint16_t flipCost;       // each Held Key moved to the other hand
            uint16_t busyCost;       // each Key beyond the first a hand already holds, when it takes another
            // A fresh hand (one that hasn't played since the last reset) has no anchor; the Split
            // Point stands in for it. Giving it any Key costs freshCost once, plus
            // wrongSideCost per half-semitone that Key is on the other hand's side.
            uint16_t freshCost;
            uint16_t wrongSideCost;
            // A Key whose two hands' costs were closer than this was a close call: while it's
            // held, a later Key can move it to the other hand.
            uint16_t closeCall;
            // When both hands cost within this of each other, the new Key goes to the hand whose
            // last Key was played at the nearer velocity.
            uint16_t velocityTie;
        };

        static const uint8_t MAX_KEYS = 128;

        class Tracker {
        public:
            explicit Tracker(const Config &config);

            // forgets every Held Key and where the hands were
            void reset();

            // A Key went down, at MIDI velocity 1-127 (0 = unknown). Returns its hand. Other Held
            // Keys may move to the other hand; movedCount()/moved() list them until the next press.
            Hand press(uint8_t key, uint8_t velocity = 0);
            void release(uint8_t key);

            bool isHeld(uint8_t key) const { return key < MAX_KEYS && held[key]; }
            Hand handOf(uint8_t key) const { return hand[key]; }

            uint8_t movedCount() const { return nMoved; }
            const uint8_t *moved() const { return movedKeys; }

        private:
            struct Range {
                int16_t lo, hi; // in half-semitones (key * 2)
            };

            Config cfg;
            bool held[MAX_KEYS];
            Hand hand[MAX_KEYS];
            uint16_t margin[MAX_KEYS];  // how much cheaper its hand was than the other, when pressed
            Range anchor[2];
            bool fresh[2];              // hasn't played since reset(): no anchor yet
            // Where each hand stood when every Key was last up. Every Held Key was pressed since,
            // so a hand that gives Keys away is judged from here.
            Range groupAnchor[2];
            bool groupFresh[2];
            uint8_t lastVelocity[2];    // of the hand's last Key; 0 = none yet
            uint8_t movedKeys[MAX_KEYS];
            uint8_t nMoved;

            int32_t wrongSide(uint8_t key, uint8_t h) const;
            int32_t fingerCost(uint8_t h, uint8_t count, const uint8_t *low, const uint8_t *high) const;
            int32_t cost(const uint8_t *keys, const Hand *assign, uint8_t n, uint8_t newKey) const;
            bool mayMove(uint8_t key) const;
            void startGroup();
        };
    }
}

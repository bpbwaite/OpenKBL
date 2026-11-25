// PC tests for the hand tracker (src/hands.cpp). Needs only a C++17 compiler; from
// keyboard-lights-firmware/:
//
//   g++ -std=c++17 -Wall -Isrc test/test_hands/test_hands.cpp src/hands.cpp -o test_hands && ./test_hands
//
// (on Windows without a native g++: run the same line under `wsl`)

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "hands.h"

using okbl::hands::Config;
using okbl::hands::Hand;
using okbl::hands::Tracker;

// matches okbl::hands in app_conf.h
static const uint8_t LOWEST_NOTE = 21; // A0
static const Config CONFIG = {
    60 - LOWEST_NOTE, // Split Point: C4 is the lowest right-hand key
    17, 12, 2,        // reach: whole hand, fingers 2-5, thumb pair
    24, 8,            // fingers 2-5 past their reach (per semitone), thumb pair
    24, 12,           // crossed hands, past where the empty hand last was
    12, 3,            // moving a Held Key, per Key held beyond one
    4, 5,             // fresh hand: once, per half-semitone on the wrong side
    16, 2,            // close call, velocity tie
};

static int failures = 0;
static int checks = 0;
static std::string currentTest;

// scientific pitch notation (C4, F#3, Bb2) -> key index
static uint8_t key(const char *name)
{
    static const int PC[] = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
    int pc = PC[name[0] - 'A'];
    const char *p = name + 1;
    if (*p == '#') { pc++; p++; }
    else if (*p == 'b') { pc--; p++; }
    int note = (atoi(p) + 1) * 12 + pc;
    return note - LOWEST_NOTE;
}

static const char *handName(Hand h) { return h == Hand::Left ? "L" : "R"; }

static void expectHand(Tracker &t, const char *note, Hand want, int line)
{
    checks++;
    Hand got = t.handOf(key(note));
    if (got != want)
    {
        failures++;
        printf("  FAIL %s (line %d): %s is %s, expected %s\n", currentTest.c_str(), line, note, handName(got), handName(want));
    }
}
#define EXPECT(t, note, h) expectHand(t, note, Hand::h, __LINE__)

static void expectTrue(bool ok, const char *what, int line)
{
    checks++;
    if (!ok)
    {
        failures++;
        printf("  FAIL %s (line %d): %s\n", currentTest.c_str(), line, what);
    }
}
#define EXPECT_TRUE(cond) expectTrue(cond, #cond, __LINE__)

// velocity from a "C4@90" token; 0 (unknown) without one
static uint8_t velocityOf(const char *tok)
{
    const char *at = strchr(tok, '@');
    return at ? atoi(at + 1) : 0;
}

// Plays a space-separated script: "C4" presses, "C4@90" presses at velocity 90, "-C4" releases.
static void play(Tracker &t, const char *script)
{
    char buf[1024];
    strncpy(buf, script, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    for (char *tok = strtok(buf, " "); tok; tok = strtok(nullptr, " "))
    {
        if (tok[0] == '-')
            t.release(key(tok + 1));
        else
            t.press(key(tok), velocityOf(tok));
    }
}

// Plays a script where every press is labelled with its true hand: "L:C3 R:E4 -C3".
// Returns how many presses the tracker got wrong at the moment of pressing, and checks that by
// the time each key is released its hand is right (corrections are allowed).
struct Score {
    int presses = 0, wrongAtPress = 0, wrongAtRelease = 0;
};
static Score playLabelled(Tracker &t, const char *script, bool verbose = false)
{
    Score s;
    Hand truth[128];
    char buf[4096];
    strncpy(buf, script, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    for (char *tok = strtok(buf, " "); tok; tok = strtok(nullptr, " "))
    {
        if (tok[0] == '-')
        {
            uint8_t k = key(tok + 1);
            if (t.handOf(k) != truth[k])
            {
                s.wrongAtRelease++;
                if (verbose)
                    printf("    released %s as %s\n", tok + 1, handName(t.handOf(k)));
            }
            t.release(k);
            continue;
        }
        Hand h = tok[0] == 'L' ? Hand::Left : Hand::Right;
        uint8_t k = key(tok + 2);
        truth[k] = h;
        s.presses++;
        if (t.press(k, velocityOf(tok)) != h)
        {
            s.wrongAtPress++;
            if (verbose)
                printf("    pressed %s as %s\n", tok + 2, handName(t.handOf(k)));
        }
    }
    // keys still held at the end
    for (uint8_t k = 0; k < 128; k++)
    {
        if (t.isHeld(k) && t.handOf(k) != truth[k])
            s.wrongAtRelease++;
    }
    return s;
}

#define TEST(name) static void name()
#define RUN(name) do { currentTest = #name; name(); } while (0)

// ---- the Split Point ----

TEST(freshHandsUseSplitPoint)
{
    Tracker t(CONFIG);
    play(t, "B3");
    EXPECT(t, "B3", Left);
    t.reset();
    play(t, "C4");
    EXPECT(t, "C4", Right);
    t.reset();
    play(t, "A0");
    EXPECT(t, "A0", Left);
    t.reset();
    play(t, "C8");
    EXPECT(t, "C8", Right);
}

TEST(freshTwoHandChord)
{
    Tracker t(CONFIG);
    play(t, "E4 C3 G4 C5 G3");
    EXPECT(t, "C3", Left);
    EXPECT(t, "G3", Left);
    EXPECT(t, "E4", Right);
    EXPECT(t, "G4", Right);
    EXPECT(t, "C5", Right);
}

// ---- reach ----

TEST(farKeyAboveRightHandIsLeftHand)
{
    // the rule from the brief: RH holds C5, a key out of its reach must be the LH, even above it
    Tracker t(CONFIG);
    play(t, "C3 -C3 C5 G6");
    EXPECT(t, "C5", Right);
    EXPECT(t, "G6", Left);
}

TEST(leftHandCrossesOverHeldChord)
{
    Tracker t(CONFIG);
    play(t, "C3 -C3 C4 E4 G4 C6");
    EXPECT(t, "C4", Right);
    EXPECT(t, "G4", Right);
    EXPECT(t, "C6", Left);
}

TEST(crossedHandsUncrossOnFullRelease)
{
    // LH crosses over the RH, then everything is let go: the hands must not stay swapped
    Tracker t(CONFIG);
    play(t, "C3 -C3 C4 E4 G4 C6");
    EXPECT(t, "C6", Left);
    play(t, "-C4 -E4 -G4 -C6 C2");
    EXPECT(t, "C2", Left);
    play(t, "E5");
    EXPECT(t, "E5", Right);
}

TEST(crossedHandsSwapPlacesOnFullRelease)
{
    // up high, the LH crosses over the RH's chord. Once everything is up, the hands swap places
    // rather than starting over from the Split Point: F5 is near where the RH chord was, so it's
    // the LH's now (the Split Point alone would make it RH).
    Tracker t(CONFIG);
    play(t, "C3 -C3 C6 E6 G6 C8");
    EXPECT(t, "C8", Left);
    play(t, "-C6 -E6 -G6 -C8 F5");
    EXPECT(t, "F5", Left);
    play(t, "B7");
    EXPECT(t, "B7", Right);
}

TEST(crossedHandsStayWhileAnyKeyHeld)
{
    // still crossed while the RH holds G4; the LH's next high note is still the LH
    Tracker t(CONFIG);
    play(t, "C3 -C3 C4 E4 G4 C6 -C4 -E4 -C6 D6");
    EXPECT(t, "D6", Left);
}

TEST(orderedHandsKeepPlaceOnFullRelease)
{
    // hands in normal order are remembered across a full release: the RH carries on below C4
    Tracker t(CONFIG);
    play(t, "C2 E4 -C2 -E4 B3");
    EXPECT(t, "B3", Right);
}

TEST(rightHandCrossesUnderHeldLeftChord)
{
    Tracker t(CONFIG);
    play(t, "C5 -C5 C3 E3 G3 C1");
    EXPECT(t, "G3", Left);
    EXPECT(t, "C1", Right);
}

TEST(eleventhFitsInOneHand)
{
    Tracker t(CONFIG);
    play(t, "C2 F3"); // exactly an eleventh, both below the split
    EXPECT(t, "C2", Left);
    EXPECT(t, "F3", Left);
}

TEST(twelfthDoesNot)
{
    Tracker t(CONFIG);
    play(t, "C2 G3");
    EXPECT(t, "C2", Left);
    EXPECT(t, "G3", Right);
}

// ---- fingers ----

TEST(keyPastThumbOfNinthIsOtherHand)
{
    // RH holds a ninth (thumb and 4 or 5). A key a step below the thumb would leave fingers 2-5
    // spanning the ninth: not the RH.
    Tracker t(CONFIG);
    play(t, "C3 -C3 C4 D5 B3");
    EXPECT(t, "D5", Right);
    EXPECT(t, "B3", Left);
}

TEST(keyPastThumbOfOctaveFitsFingers)
{
    // an octave is within FINGER_SPAN (12): B3 under an RH octave can still be the RH thumb
    Tracker t(CONFIG);
    play(t, "C2 -C2 C4 C5 B3");
    EXPECT(t, "B3", Right);
}

TEST(keyInsideNinthIsSameHand)
{
    Tracker t(CONFIG);
    play(t, "C3 -C3 C4 D5 E4 G4");
    for (const char *n : {"C4", "E4", "G4", "D5"})
        EXPECT(t, n, Right);
}

TEST(keyPastLeftThumbIsOtherHand)
{
    // mirrored: the LH thumb is its highest key. LH holds C2-D3 (a ninth); D#3 above the thumb
    // would leave fingers 2-5 spanning the ninth.
    Tracker t(CONFIG);
    play(t, "C5 -C5 C2 D3 D#3");
    EXPECT(t, "D3", Left);
    EXPECT(t, "D#3", Right);
}

TEST(keyPastPinkyStillFits)
{
    // reaching past the pinky end moves only fingers 2-5 outward: fine within the eleventh
    Tracker t(CONFIG);
    play(t, "C3 -C3 C4 E4 G4 C5 E5");
    EXPECT(t, "E5", Right);
}

// ---- Key count ----

TEST(sixKeysWithBlackThumbPair)
{
    // La Campanella-style: the thumb takes C#-D#
    Tracker t(CONFIG);
    play(t, "C2 -C2 C#4 D#4 F#4 A#4 C#5 D#5");
    for (const char *n : {"C#4", "D#4", "F#4", "A#4", "C#5", "D#5"})
        EXPECT(t, n, Right);
}

TEST(sixthKeyWithoutThumbPairIsOtherHand)
{
    // C4 and E4 are too far apart for one thumb, so the RH has no finger for a 6th key
    Tracker t(CONFIG);
    play(t, "C2 -C2 C4 E4 G4 A4 C5 D5");
    EXPECT(t, "C5", Right);
    EXPECT(t, "D5", Left);
}

TEST(sixKeyChordInOneHand)
{
    // Rachmaninoff-style: the thumb takes two keys
    Tracker t(CONFIG);
    play(t, "C5 -C5 C2 E2 G2 Bb2 C3 D3");
    for (const char *n : {"C2", "E2", "G2", "Bb2", "C3", "D3"})
        EXPECT(t, n, Left);
}

TEST(seventhKeyGoesToOtherHand)
{
    Tracker t(CONFIG);
    play(t, "C5 -C5 C2 E2 G2 Bb2 C3 D3 E3");
    EXPECT(t, "C2", Left);
    EXPECT(t, "E3", Right);
}

// ---- corrections ----

TEST(closeCallMovedByLaterKey)
{
    // RH G major, B3-D4-G4, with nothing to go on: B3 is a close call by the Split Point and goes
    // left. When the LH plays G2, B3 sits better in the RH chord, so it moves.
    Tracker t(CONFIG);
    play(t, "B3 D4 G4");
    EXPECT(t, "B3", Left);
    play(t, "G2");
    EXPECT(t, "G2", Left);
    EXPECT(t, "B3", Right);
    EXPECT_TRUE(t.movedCount() == 1);
}

TEST(confidentKeyNotMoved)
{
    // the rule from the brief, with the hands placed: RH at C5 is no close call, so a far G6 is
    // the LH crossing over rather than the RH taking G6 and the LH C5
    Tracker t(CONFIG);
    play(t, "C3 E5 -C3 -E5 C5 G6");
    EXPECT(t, "C5", Right);
    EXPECT(t, "G6", Left);
}

// ---- empty hands, velocity ----

TEST(emptyHandStillBlocksCrossing)
{
    // D4 is RH; G#5 is out of its reach, so the LH crossed over. With the RH's D4 let go, F4 is
    // near where the RH was, and the LH would have to reach back under it: the RH.
    Tracker t(CONFIG);
    play(t, "D4 G#5");
    EXPECT(t, "G#5", Left);
    play(t, "-D4 F4");
    EXPECT(t, "F4", Right);
}

TEST(velocityBreaksTie)
{
    // LH last played soft, RH loud; a key equally near both goes to the matching dynamic
    Tracker t(CONFIG);
    play(t, "B3@30 D#4@110 -B3 -D#4 C#4@105");
    EXPECT(t, "C#4", Right);
    t.reset();
    play(t, "B3@30 D#4@110 -B3 -D#4 C#4@35");
    EXPECT(t, "C#4", Left);
}

TEST(velocityNeverOverridesPosition)
{
    Tracker t(CONFIG);
    play(t, "C2@30 C5@110 -C2 -C5 D2@110");
    EXPECT(t, "D2", Left);
}

TEST(guessCorrected)
{
    // RH A3-C4-E4 with nothing to go on: A3 is below the Split Point, so it looks like LH. When
    // the LH plays A1, A3 can't be in the same hand (too far), so it moves to the right hand.
    Tracker t(CONFIG);
    play(t, "A3 C4 E4");
    EXPECT(t, "A3", Left);
    EXPECT(t, "E4", Right);
    play(t, "A1");
    EXPECT(t, "A1", Left);
    EXPECT(t, "A3", Right);
    EXPECT(t, "C4", Right);
    EXPECT(t, "E4", Right);
    EXPECT_TRUE(t.movedCount() == 1 && t.moved()[0] == key("A3"));
}

TEST(lastHandStaysPutAcrossSplit)
{
    // LH plays G3 then B3: the third above is the same hand, even though the RH is fresh
    Tracker t(CONFIG);
    play(t, "G3 B3");
    EXPECT(t, "B3", Left);
    t.reset();
    play(t, "C2 -C2 F3"); // LH well down, F3 still on its side and nothing says RH
    EXPECT(t, "F3", Left);
}

TEST(noChurnWhenAlreadyRight)
{
    Tracker t(CONFIG);
    play(t, "C3 E3 G3 C5 E5");
    play(t, "G5");
    EXPECT_TRUE(t.movedCount() == 0);
    EXPECT(t, "G5", Right);
    EXPECT(t, "G3", Left);
}

// ---- following the hands ----

TEST(busyHandDoesNotTakeWalkingNotes)
{
    // RH holds a 4-note chord and stays still; the LH walks up from low until just under it
    Tracker t(CONFIG);
    play(t, "C4 E4 G4 C5");
    const char *walk[] = {"C3", "D3", "E3", "F3", "G3", "A3", "B3"};
    for (const char *n : walk)
    {
        play(t, n);
        EXPECT(t, n, Left);
        t.release(key(n));
    }
    EXPECT(t, "C4", Right);
}

TEST(rightHandWalksDownAcrossSplit)
{
    Tracker t(CONFIG);
    play(t, "C2 -C2"); // LH was down low
    const char *scale[] = {"C5", "B4", "A4", "G4", "F4", "E4", "D4", "C4", "B3", "A3", "G3", "F3", "E3"};
    for (const char *n : scale)
    {
        play(t, n);
        EXPECT(t, n, Right);
        t.release(key(n));
    }
}

TEST(leftHandWalksUpAcrossSplit)
{
    Tracker t(CONFIG);
    play(t, "C6 -C6");
    const char *scale[] = {"C3", "D3", "E3", "F3", "G3", "A3", "B3", "C4", "D4", "E4", "F4"};
    for (const char *n : scale)
    {
        play(t, n);
        EXPECT(t, n, Left);
        t.release(key(n));
    }
}

TEST(repeatedPressIsHarmless)
{
    Tracker t(CONFIG);
    play(t, "C3 C3 C3");
    EXPECT(t, "C3", Left);
    EXPECT_TRUE(t.isHeld(key("C3")));
}

TEST(impossibleChordDoesNotCrash)
{
    Tracker t(CONFIG);
    play(t, "A0 C4 C8 C2 C6");
    EXPECT_TRUE(t.isHeld(key("C8")));
}

// ---- labelled passages: accuracy ----

struct Passage {
    const char *name;
    const char *script;
};

static const Passage PASSAGES[] = {
    {"alberti bass + melody",
     "L:C3 R:E5 -C3 L:G3 -G3 L:E3 -E3 L:G3 -G3 -E5 R:D5 L:C3 -C3 L:G3 -G3 L:E3 -E3 L:G3 -G3 -D5 "
     "R:C5 L:B2 -B2 L:G3 -G3 L:D3 -D3 L:G3 -G3 -C5 R:B4 L:C3 -C3 L:G3 -G3 L:E3 -E3 L:G3 -G3 -B4"},
    {"close hands near middle C",
     "L:A3 R:C4 R:E4 -A3 L:F3 -C4 R:D4 -E4 R:F4 -F3 L:G3 -D4 -F4 R:E4 R:G4 -G3 L:B3 -E4 -G4 R:C4 R:D4 -B3 -C4 -D4"},
    {"stride left hand",
     "L:C2 R:E4 R:G4 -C2 L:C3 L:E3 L:G3 -C3 -E3 -G3 -E4 -G4 L:G1 R:F4 R:A4 -G1 L:B2 L:D3 L:F3 -B2 -D3 -F3 -F4 -A4 "
     "L:C2 R:E4 R:C5 -C2 L:C3 L:E3 L:G3"},
    {"big chords both hands",
     "L:C#2 L:C#3 R:G#4 R:C#5 R:E5 R:G#5 -C#2 -C#3 -G#4 -C#5 -E5 -G#5 "
     "L:B1 L:B2 R:F#4 R:A4 R:D#5 R:F#5 -B1 -B2 -F#4 -A4 -D#5 -F#5 "
     "R:A4 L:A1 R:C#5 L:A2 R:E5 R:A5"},
    {"hand crossing arpeggio",
     "L:C2 L:G2 L:C3 -C2 -G2 -C3 R:E4 R:G4 R:C5 L:E5 -E4 -G4 -C5 -E5 "
     "L:C2 L:G2 L:C3 -C2 -G2 -C3 R:E4 R:G4 R:C5 L:E5"},
    {"contrary motion scales",
     "L:C3 R:C5 -C3 -C5 L:B2 R:D5 -B2 -D5 L:A2 R:E5 -A2 -E5 L:G2 R:F5 -G2 -F5 L:F2 R:G5 -F2 -G5 "
     "L:G2 R:F5 -G2 -F5 L:A2 R:E5 -A2 -E5 L:B2 R:D5 -B2 -D5 L:C3 R:C5"},
    {"parallel scales an octave apart, close",
     "L:C3 R:C4 -C3 -C4 L:D3 R:D4 -D3 -D4 L:E3 R:E4 -E3 -E4 L:F3 R:F4 -F3 -F4 L:G3 R:G4 -G3 -G4 "
     "L:A3 R:A4 -A3 -A4 L:B3 R:B4 -B3 -B4 L:C4 R:C5 -C4 -C5 L:D4 R:D5 -D4 -D5 L:E4 R:E5"},
};

static void accuracy()
{
    printf("\nlabelled passages (wrong when pressed / wrong when released):\n");
    int presses = 0, atPress = 0, atRelease = 0;
    for (const Passage &p : PASSAGES)
    {
        Tracker t(CONFIG);
        Score s = playLabelled(t, p.script, getenv("VERBOSE") != nullptr);
        printf("  %-40s %2d/%2d  %2d/%2d\n", p.name, s.wrongAtPress, s.presses, s.wrongAtRelease, s.presses);
        presses += s.presses;
        atPress += s.wrongAtPress;
        atRelease += s.wrongAtRelease;
    }
    printf("  %-40s %2d/%2d  %2d/%2d\n", "total", atPress, presses, atRelease, presses);
}

int main()
{
    RUN(freshHandsUseSplitPoint);
    RUN(freshTwoHandChord);
    RUN(farKeyAboveRightHandIsLeftHand);
    RUN(leftHandCrossesOverHeldChord);
    RUN(crossedHandsUncrossOnFullRelease);
    RUN(crossedHandsSwapPlacesOnFullRelease);
    RUN(crossedHandsStayWhileAnyKeyHeld);
    RUN(orderedHandsKeepPlaceOnFullRelease);
    RUN(rightHandCrossesUnderHeldLeftChord);
    RUN(eleventhFitsInOneHand);
    RUN(twelfthDoesNot);
    RUN(keyPastThumbOfNinthIsOtherHand);
    RUN(keyPastThumbOfOctaveFitsFingers);
    RUN(keyInsideNinthIsSameHand);
    RUN(keyPastLeftThumbIsOtherHand);
    RUN(keyPastPinkyStillFits);
    RUN(sixKeysWithBlackThumbPair);
    RUN(sixthKeyWithoutThumbPairIsOtherHand);
    RUN(closeCallMovedByLaterKey);
    RUN(confidentKeyNotMoved);
    RUN(emptyHandStillBlocksCrossing);
    RUN(velocityBreaksTie);
    RUN(velocityNeverOverridesPosition);
    RUN(sixKeyChordInOneHand);
    RUN(seventhKeyGoesToOtherHand);
    RUN(guessCorrected);
    RUN(lastHandStaysPutAcrossSplit);
    RUN(noChurnWhenAlreadyRight);
    RUN(busyHandDoesNotTakeWalkingNotes);
    RUN(rightHandWalksDownAcrossSplit);
    RUN(leftHandWalksUpAcrossSplit);
    RUN(repeatedPressIsHarmless);
    RUN(impossibleChordDoesNotCrash);

    accuracy();

    printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}

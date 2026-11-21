# Hand Colors

Status: ready-for-agent

A new Color option, `Color::Hands`, that lights the left hand's Keys blue (0x0000FF) and the right
hand's green (0x00FF00). Brightness and Pedal Fade apply as usual. AllOctaves compatibility is out of
scope for now.

## Decisions

- Split Point B3/C4: with nothing else to go on, B3 and below are LH, C4 and above RH.
  `kbl::hands::SPLIT_NOTE` in `app_conf.h`.
- Each hand reaches at most an eleventh (17 semitones), lowest to highest Held Key.
- Fingers aren't tracked; each hand's Held Keys imply them. The thumb is on the inner end (RH
  lowest, LH highest). Fingers 2-5 reach at most an octave (`fingerSpan`, try a seventh if Keys
  near a stretched thumb land wrong) and hold at most 4 Keys. The thumb holds two Keys up to 2
  semitones apart (white or black: C-D, E-F, C#-D#), only when needed for a 6th Key.
- Sustained Keys don't constrain a hand: the finger has left them. They keep their color.
- Timing is never used.
- Held Keys are re-colored when later notes prove the earlier guess wrong. With
  `-DKBL_HAND_DEBUG`, a corrected Key flashes red briefly.
- Hand memory resets only when Key state is cleared (Settings Menu opens, Demo Mode starts, All
  Notes Off), not after silences.
- When every Key is up and the LH's anchor is above the RH's, the two anchors swap, so a
  crossing (or a wrong guess) can't leave the hands stuck swapped. Hands in their usual order keep
  their anchors across a full release.
- A hand holding a chord is less likely to take another Key: `BUSY_COST` per Key it holds beyond
  the first.
- Close calls (Keys whose hand won by less than `closeCall`) can be moved by a later Key while
  held, when a cut scores better. Confident Keys move only when nothing else fits.
- Velocity breaks near ties (`velocityTie`): the hand whose last Key was nearer in velocity.

## Algorithm (`src/hands.{h,cpp}`, pure C++, tested on the PC)

State per hand: its Held Keys, and an **anchor**: the range (lowest..highest) of its Held Keys
right after the last press assigned to it. Releases don't move the anchor, so a chord released in
any order leaves it where the hand was. A fresh hand (one that hasn't played yet) has no anchor:
giving it a Key costs `FRESH_COST` once plus `WRONG_SIDE_COST` per half-semitone the Key is on the
other side of the Split Point, which reproduces the split rule exactly when both hands are fresh.

On each press, score a handful of candidate assignments of all Held Keys plus the new one and
keep the cheapest:

- keep every current assignment, new Key to LH;
- the same, new Key to RH;
- every split point of the sorted Held Keys (lowest k to LH, rest to RH), which is how earlier
  wrong guesses are corrected.

Cost, in half-semitones:

- each Key that is new or changes hand: its distance from that hand's anchor range. A hand that
  gives Keys away is judged from where it stood when every Key was last up, since its anchor was
  set by the Keys it's giving up;
- each Key that changes hand: `FLIP_COST`;
- a hand spanning more than `MAX_SPAN`, or holding more than `MAX_KEYS`: infeasible
  (a large cost plus the overflow, so the least-bad choice still wins if nothing is feasible);
- fingers 2-5 (all but the thumb's Key) spanning past `fingerSpan`: `fingerOverCost` per
  semitone; more than 4 of them: infeasible, unless the thumb takes two (`thumbPairCost`);
- hands crossed (highest LH Key above lowest RH Key): `CROSS_COST`;
- one hand empty, the other's Keys past where it last played: `idleCrossCost`;
- the hand taking the new Key already holds k > 1 Keys: `BUSY_COST` × (k − 1).

The cross-over rule falls out of the span limit: if the RH holds C5, a G6 can't be the RH.

## Firmware

- `KeyState.hand`, set on press; corrected Keys updated in place.
- `Lit` carries the hand to `colorOf`; Demo Mode and the Preview assign hands by the Split Point.
- Settings Menu: a fourth Color Option, "Hands", whose Swatch alternates blue and green.
- Docs: CONTEXT.md (Hand, Split Point), README.

## Testing

`test/test_hands/test_hands.cpp`: scripted note sequences against the tracker, built with any host
g++ (see the file header). Later: an evaluator scoring accuracy on two-track (LH/RH) MIDI files to
tune the costs.

## Comments

- 2026-11-20: Hardware testing showed hands getting swapped and staying swapped after every key
  was released. Fixed by un-crossing on a full release (decision above).
- 2026-11-20: Un-crossing now swaps the anchors instead of resetting to the Split Point. Added
  `BUSY_COST`: an RH holding a 4-note chord was taking the top of an LH walking up under it.
- 2026-11-20: Added finger rules (fingers 2-5 reach, thumb pairs), close-call corrections,
  crossing past an empty hand, and velocity as a tie-breaker. A random search found the empty-hand
  crossing cost changes about 1 decision in 2000 short sequences: distance already keeps hands in
  order, so it only acts once they're crossed.

#include "hands.h"

namespace okbl {
    namespace hands {
        namespace {
            // A choice that breaks a hard limit (reach, or more Keys than fingers) costs at least
            // this, plus the overflow, so when every choice breaks one the least-bad still wins.
            const int32_t INFEASIBLE = 100000;
            const int32_t OVERFLOW_COST = 1000; // per semitone or Key over the limit
            const uint8_t FINGER_KEYS = 4;      // fingers 2-5

            const uint8_t L = (uint8_t)Hand::Left;
            const uint8_t R = (uint8_t)Hand::Right;

            int32_t distance(int16_t point, int16_t lo, int16_t hi)
            {
                return point < lo ? lo - point : point > hi ? point - hi : 0;
            }

            int32_t absDiff(int32_t a, int32_t b)
            {
                return a > b ? a - b : b - a;
            }
        }

        Tracker::Tracker(const Config &config) : cfg(config)
        {
            reset();
        }

        void Tracker::reset()
        {
            for (uint8_t k = 0; k < MAX_KEYS; k++)
            {
                held[k] = false;
                hand[k] = k < cfg.splitKey ? Hand::Left : Hand::Right;
                margin[k] = 0;
            }
            fresh[L] = fresh[R] = true;
            anchor[L] = anchor[R] = {0, 0};
            lastVelocity[L] = lastVelocity[R] = 0;
            nMoved = 0;
            startGroup();
        }

        void Tracker::startGroup()
        {
            for (uint8_t h = 0; h < 2; h++)
            {
                groupAnchor[h] = anchor[h];
                groupFresh[h] = fresh[h];
            }
        }

        // a fresh hand's cost for a Key: how far it is on the other hand's side of the Split Point
        int32_t Tracker::wrongSide(uint8_t key, uint8_t h) const
        {
            int16_t boundary = cfg.splitKey * 2 - 1; // between the last left key and the first right
            int16_t over = h == L ? key * 2 - boundary : boundary - key * 2;
            return over > 0 ? (int32_t)over * cfg.wrongSideCost : 0;
        }

        // What a hand's Keys cost its fingers. low: its lowest three Keys, ascending; high: its
        // highest three, descending (fewer are valid when count < 3). The thumb is on the inner
        // end: the right hand's lowest Key, the left hand's highest. Fingers 2-5 take the rest.
        int32_t Tracker::fingerCost(uint8_t h, uint8_t count, const uint8_t *low, const uint8_t *high) const
        {
            if (count < 2)
                return 0;

            // thumb, then the Key next to it, then the one after, from the thumb's end inward
            const uint8_t *inner = h == R ? low : high;
            // the outermost Key, at the pinky's end
            uint8_t outer = h == R ? high[0] : low[0];

            auto fingers = [&](uint8_t thumbKeys) -> int32_t {
                uint8_t fingerKeys = count - thumbKeys;
                int32_t c = 0;
                if (fingerKeys > FINGER_KEYS)
                    c += INFEASIBLE + (int32_t)(fingerKeys - FINGER_KEYS) * OVERFLOW_COST;
                if (fingerKeys >= 2)
                {
                    uint8_t first = inner[thumbKeys]; // the Key nearest the thumb's
                    uint8_t span = first > outer ? first - outer : outer - first;
                    if (span > cfg.fingerSpan)
                        c += (int32_t)(span - cfg.fingerSpan) * cfg.fingerOverCost;
                }
                return c;
            };

            // The thumb takes two Keys only when the fingers run out, as in a six-note chord.
            // Otherwise a Key a step past the thumb would always look like a thumb pair, and the
            // hand could never be "full" at its thumb end.
            if (count - 1 > FINGER_KEYS && absDiff(inner[0], inner[1]) <= cfg.thumbPairSpan)
                return fingers(2) + cfg.thumbPairCost;
            return fingers(1);
        }

        // keys: every Held Key plus newKey, ascending. assign: a hand for each.
        int32_t Tracker::cost(const uint8_t *keys, const Hand *assign, uint8_t n, uint8_t newKey) const
        {
            int32_t total = 0;
            uint8_t count[2] = {0, 0};
            uint8_t low[2][3] = {}, high[2][3] = {};
            uint8_t newHand = R;

            // A hand that gives Keys away here is judged from where it stood before them: its
            // anchor was set by the very Keys it's giving up.
            bool givesAway[2] = {false, false};
            for (uint8_t i = 0; i < n; i++)
            {
                if (keys[i] != newKey && assign[i] != hand[keys[i]])
                    givesAway[(uint8_t)hand[keys[i]]] = true;
            }
            Range at[2];
            bool isFresh[2];
            for (uint8_t h = 0; h < 2; h++)
            {
                at[h] = givesAway[h] ? groupAnchor[h] : anchor[h];
                isFresh[h] = givesAway[h] ? groupFresh[h] : fresh[h];
            }

            for (uint8_t i = 0; i < n; i++)
            {
                uint8_t k = keys[i];
                uint8_t h = (uint8_t)assign[i];
                if (count[h] < 3)
                    low[h][count[h]] = k;
                high[h][2] = high[h][1];
                high[h][1] = high[h][0];
                high[h][0] = k;
                count[h]++;

                bool isNew = k == newKey;
                bool flipped = !isNew && assign[i] != hand[k];
                if (isNew)
                    newHand = h;
                if (isNew || flipped)
                    total += isFresh[h] ? wrongSide(k, h) : distance(k * 2, at[h].lo, at[h].hi);
                if (flipped)
                    total += cfg.flipCost;
            }

            for (uint8_t h = 0; h < 2; h++)
            {
                if (!count[h])
                    continue;
                if (isFresh[h])
                    total += cfg.freshCost;
                uint8_t span = high[h][0] - low[h][0];
                if (span > cfg.maxSpan)
                    total += INFEASIBLE + (int32_t)(span - cfg.maxSpan) * OVERFLOW_COST;
                total += fingerCost(h, count[h], low[h], high[h]);
            }

            // A hand holding a chord has fewer free fingers, and is more likely holding still.
            // Holding one Key and adding another is ordinary, so this starts from the second.
            if (count[newHand] > 2)
                total += (int32_t)(count[newHand] - 2) * cfg.busyCost;

            if (count[L] && count[R])
            {
                if (high[L][0] > low[R][0])
                    total += cfg.crossCost;
            }
            // an empty hand still stands where it last played; reaching past it crosses the hands
            else if (count[L] && !isFresh[R])
            {
                if (high[L][0] * 2 > at[R].lo)
                    total += cfg.idleCrossCost;
            }
            else if (count[R] && !isFresh[L])
            {
                if (low[R][0] * 2 < at[L].hi)
                    total += cfg.idleCrossCost;
            }
            return total;
        }

        bool Tracker::mayMove(uint8_t key) const
        {
            return margin[key] < cfg.closeCall;
        }

        Hand Tracker::press(uint8_t key, uint8_t velocity)
        {
            nMoved = 0;
            if (key >= MAX_KEYS)
                return Hand::Right;
            held[key] = false; // a repeated press without a release is just a new press

            uint8_t keys[MAX_KEYS];
            uint8_t n = 0;
            uint8_t newIndex = 0;
            for (uint16_t k = 0; k < MAX_KEYS; k++)
            {
                if (k == key)
                {
                    newIndex = n;
                    keys[n++] = key;
                }
                else if (held[k])
                {
                    keys[n++] = k;
                }
            }

            Hand best[MAX_KEYS];
            Hand trial[MAX_KEYS];

            // keep every Held Key where it is; the new Key goes to either hand
            for (uint8_t i = 0; i < n; i++)
                trial[i] = hand[keys[i]];
            trial[newIndex] = Hand::Left;
            int32_t leftCost = cost(keys, trial, n, key);
            trial[newIndex] = Hand::Right;
            int32_t rightCost = cost(keys, trial, n, key);

            Hand chosen = leftCost <= rightCost ? Hand::Left : Hand::Right;
            int32_t diff = absDiff(leftCost, rightCost);
            // a near tie: the hand that last played at the nearer velocity
            if (diff <= cfg.velocityTie && leftCost < INFEASIBLE && rightCost < INFEASIBLE &&
                velocity && lastVelocity[L] && lastVelocity[R])
            {
                int32_t toLeft = absDiff(velocity, lastVelocity[L]);
                int32_t toRight = absDiff(velocity, lastVelocity[R]);
                if (toLeft != toRight)
                    chosen = toLeft < toRight ? Hand::Left : Hand::Right;
            }
            for (uint8_t i = 0; i < n; i++)
                best[i] = hand[keys[i]];
            best[newIndex] = chosen;
            int32_t bestCost = chosen == Hand::Left ? leftCost : rightCost;

            // Or cut the keyboard between two Held Keys, everything below going left. When the new
            // Key fits either hand, a cut may only move close calls: Held Keys are otherwise trusted, so a far Key goes to the other hand (crossing
            // over) rather than reshuffling the hands to fit it. When it fits neither, an earlier
            // guess was wrong, and any Key may move.
            bool anyMayMove = bestCost >= INFEASIBLE;
            for (uint8_t cut = 0; cut <= n; cut++)
            {
                bool allowed = true;
                for (uint8_t i = 0; i < n; i++)
                {
                    trial[i] = i < cut ? Hand::Left : Hand::Right;
                    if (!anyMayMove && i != newIndex && trial[i] != hand[keys[i]] && !mayMove(keys[i]))
                        allowed = false;
                }
                if (!allowed)
                    continue;
                int32_t c = cost(keys, trial, n, key);
                if (c < bestCost) // ties keep what's already there
                {
                    bestCost = c;
                    for (uint8_t i = 0; i < n; i++)
                        best[i] = trial[i];
                }
            }

            bool changed[2] = {false, false};
            for (uint8_t i = 0; i < n; i++)
            {
                uint8_t k = keys[i];
                if (k != key && best[i] != hand[k])
                {
                    movedKeys[nMoved++] = k;
                    changed[(uint8_t)hand[k]] = true;
                }
                if (k == key || best[i] != hand[k])
                    changed[(uint8_t)best[i]] = true;
                hand[k] = best[i];
            }
            held[key] = true;
            margin[key] = diff > 0xFFFF ? 0xFFFF : diff;
            if (velocity)
                lastVelocity[(uint8_t)hand[key]] = velocity;

            // a hand that gained or lost Keys is now where its Held Keys are
            for (uint8_t h = 0; h < 2; h++)
            {
                if (!changed[h])
                    continue;
                bool any = false;
                for (uint8_t i = 0; i < n; i++)
                {
                    if ((uint8_t)best[i] != h)
                        continue;
                    if (!any)
                        anchor[h].lo = keys[i] * 2;
                    anchor[h].hi = keys[i] * 2;
                    any = true;
                }
                if (any)
                {
                    fresh[h] = false;
                }
                else
                {
                    // gave all its Keys away: back to where it stood before them
                    anchor[h] = groupAnchor[h];
                    fresh[h] = groupFresh[h];
                }
            }
            return hand[key];
        }

        void Tracker::release(uint8_t key)
        {
            if (key >= MAX_KEYS || !held[key])
                return;
            held[key] = false;
            for (uint8_t k = 0; k < MAX_KEYS; k++)
            {
                if (held[k])
                    return;
            }

            // If the hands were left crossed (the left above the right), they would stay that
            // way: each next Key goes to whichever hand is nearer. So they swap places: the left
            // takes the lower of the two positions, the right the higher.
            if (!fresh[L] && !fresh[R] && anchor[L].lo + anchor[L].hi > anchor[R].lo + anchor[R].hi)
            {
                Range lower = anchor[R];
                anchor[R] = anchor[L];
                anchor[L] = lower;
            }

            // remember where the hands stand, for judging the Keys that come next
            startGroup();
        }
    }
}

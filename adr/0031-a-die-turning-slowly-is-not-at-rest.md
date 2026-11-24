---
id: '0031'
title: "A die turning at 0.1 rad/s is not at rest: the angular rest speed drops from 0.5, and every die's hold falls with it"
type: architecture
status: accepted
date: 2026-11-24
supersedes: []
superseded_by: []
---

## Context

ADR-0009 set two rest speeds: a die is at rest when it is below 5 mm/s and 0.5 rad/s. ADR-0028
then made the hold per die, measured as four times the longest pause after which that die still
tipped onto another face. Both have held for the D6, the D8, the D10 and the D20.

The D12 broke the second one in a way that pointed at the first. Over 2,000 traced rolls
(`scripts/fairness-probe.mjs`), one roll paused **0.1583 s** before tipping, against 0.0500 s for
the worst die before it. The four-times rule turns that into a 0.64 s hold, and at that hold a
three-dice D12 roll takes a mean of **1.451 s** against 0.365 s for a D6: the die would feel
broken next to every other one.

The trace grid says why. That roll's pause is 0.1583 s at 0.5 rad/s and **0.0000 s at 0.1 rad/s**:
the die was never still. It was creeping over an edge at between 0.1 and 0.5 rad/s, slowly enough
that its vertices moved under 5 mm/s, and the angular threshold called that rest. The hold was
not covering a balance; it was waiting out a rotation the thresholds had failed to notice.

The threshold has enormous room. Measured over the same traces, a die actually at rest moves at
most 0.000051 rad/s, so 0.5 rad/s is about 10,000 times the resting floor and 0.1 rad/s is still
about 2,000 times it.

The longest false rest each die shows at the two thresholds, 2,000 traced rolls each, with the
hold the four-times rule then asks for:

| Die | at 0.5 rad/s | hold | at 0.1 rad/s | hold |
|---|---|---|---|---|
| D6 | 0.0083 s | 0.04 s | 0.0000 s | 0.01 s |
| D8 | 0.0417 s | 0.17 s | 0.0000 s | 0.01 s |
| D10 | 0.0500 s | 0.21 s | 0.0167 s | 0.07 s |
| D12 | 0.1583 s | 0.64 s | 0.0083 s | 0.04 s |
| D20 | 0.0375 s | 0.16 s | 0.0083 s | 0.04 s |

And what that is worth, as the mean time a three-dice roll takes from detection to lock, 2,000
rolls of each die, with no roll left unsettled in either column:

| Die | mean roll at 0.5 rad/s | mean roll at 0.1 rad/s |
|---|---|---|
| D6 | 0.365 s | 0.334 s |
| D8 | 0.570 s | 0.372 s |
| D10 | 0.650 s | 0.471 s |
| D12 | 1.451 s | 0.528 s |
| D20 | 0.671 s | 0.478 s |

Every die is quicker, not only the D12, and the dice that gained most are the ones whose holds had
been ratcheted up by exactly this effect.

## Decision

**`REST_ANGULAR_RAD_PER_S` drops from 0.5 to 0.1.** A die turning faster than that is still
rolling, however slowly its centre moves, and a die that is still rolling has not finished.

**Every die's hold is re-measured at the new threshold** by ADR-0028's unchanged rule: D6 0.01 s,
D8 0.01 s, D10 0.07 s, D12 0.04 s, D20 0.04 s.

**Where a trace observes no false rest at all, the hold is 0.01 s**, which is what "the next
hundredth of a second strictly above four times zero" already yields, so the rule needs no special
case. It is worth being plain that this is the rule's floor and not a measured balance: the D6 and
the D8 showed no false rest in 2,000 rolls each at this threshold, which bounds their balance
below a hundredth of a second rather than measuring it. 0.01 s is 2.4 physics steps, so a result
still never locks on the strength of a single step.

**`REST_LINEAR_MM_PER_S` stays 5 and `COCKED_TILT_RAD` stays 0.5°.** Neither was implicated: the
linear speed is about 30,000 times its measured floor, and the tilt gap the traces show is
unchanged, the flattest propped D12 at 5.009° against the most tilted flat one at 0.056°.

**This replaces one claim of ADR-0009**: "a result locks when the die stays below 5 mm/s and
0.5 rad/s", as amended by slice 0026 and corrected by ADR-0028. The linear speed, the lying-flat
rule and the four-times method all stand, and so does ADR-0028's decision that the hold belongs to
the die. ADR-0009 carries a correction marker at that claim pointing here.

**Why the old reasoning was wrong.** ADR-0009 measured what a resting die does (0.00002 rad/s) and
then chose thresholds far above it, which is the right instinct: a threshold near the floor would
be noise-sensitive. What it did not do was check the other side, whether anything that is *not* at
rest also falls below the threshold. Between the floor and 0.5 rad/s lies the whole of a slow tip,
and the hold then had to be long enough to outlast one. Three dice in a row paid for that with
holds four to seventeen times longer than they needed, and each new die made it worse, because a
blunter die tips more slowly. The lesson is that a rest threshold has two sides and only one of
them was measured.

## Consequences

- Every die locks sooner. On the four dice already shipped, which is what anybody will notice,
  the three-dice mean roll falls by 0.03 s (D6), 0.18 s (D10), 0.19 s (D20) and 0.20 s (D8). The
  D12 falls by 0.92 s, but it never shipped at the longer hold, so no player will see that one.
  The four are user-visible and go in `CHANGELOG.md`.
- Changing a rest speed changes the physics' input, so invariant #2 applies to all five dice: the
  three-dice fairness test runs before it merges, not only for the D12.
- A die that genuinely creeps for a long time now keeps rolling in the product's eyes rather than
  locking early, which is the behaviour invariant #3 wants. The cost is a longer tail: the D12's
  longest three-dice roll is 2.23 s, inside the probe's 20 s limit with room to spare.
- The 0.1 rad/s figure is itself a threshold chosen above a measured floor, so it invites the same
  question this ADR asked of 0.5. What makes it defensible is the second measurement, not the
  first: at 0.1 rad/s no die's traced pause exceeds 0.0167 s, so nothing is hiding under it at the
  scale the holds would have to cover. If a later die shows a pause far above its neighbours
  again, that is the signal to look at the threshold before the hold.

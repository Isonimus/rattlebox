---
id: '0028'
title: "The lock hold is measured per die and carried on its body, not one value every die waits out"
type: architecture
status: accepted
date: 2026-11-23
supersedes: []
superseded_by: []
---

## Context

ADR-0009 chose one hold for every die: a result locks when the die has stayed below 5 mm/s and
0.5 rad/s for `REST_HOLD_S`, set at four times the longest pause after which a die still tipped
onto another face. At the time the only dice were the D6 and the D20, and one number covered both.

It has not held. The D8 pushed it from 0.15 s to 0.17 s (slice 0026, recorded as an amendment at
that claim), and tracing 2,000 D10 rolls now asks for 0.20 s. Each new die ratchets the single
number up to whatever the worst die needs, and every other die waits that out.

The cost of that is now measured. The longest pause after which each die still tipped, at the
product's thresholds, over 2,000 traced rolls each (`scripts/fairness-probe.mjs`):

| Die | Longest false rest | Four times it | Hold it needs |
|---|---|---|---|
| D6 | 0.0083 s | 0.033 s | 0.04 s |
| D20 | 0.0375 s | 0.150 s | 0.16 s |
| D8 | 0.0417 s | 0.167 s | 0.17 s |
| D10 | 0.0500 s | 0.200 s | 0.21 s |

A single hold would have to be 0.21 s. The D6 would then sit visibly still for 0.21 s before its
result appeared, 6.3 times the 0.033 s its own behaviour calls for, and about 170 ms of dead time
on the die people roll most. Two dice of v0.2 are still to come (`docs/roadmap.md`), each able to
ratchet the number again and each change visible to every player of every die.

The dihedral angle explains the spread and says it will keep spreading: a D6's faces meet at 90°
and it drops onto the next face at once, while a D10's meet at 97.96° and 124.41° and a D20's at
138.2°, so a blunter die can balance on an edge longer before it goes over.

## Decision

**The hold is a property of the die, measured from that die's own trace, and carried on its body.**

- `DieShape` gains `holdS`, beside `inertiaPerMass`, which is already a property of how the body
  behaves rather than of its outline. Each `makeXxx` sets it from a named constant in
  `src/geometry/die_shape.cpp`, beside the inertia constants and not in
  `src/physics/physics_config.h` where the rest speeds live: a hold is a fact about the body, and
  reaching into the physics config from the geometry layer would invert their dependency.
  `physics_config.h` keeps a comment pointing here.
- `RestThresholds` loses `holdS`. What remains in it, the two rest speeds and the cocked tilt, is
  what every die genuinely shares, so `PRODUCT_REST_THRESHOLDS` stays one value and every caller
  of it is unchanged. `updateResult` reads the hold from the shape it was already given.
- The hold for a die is **the next hundredth of a second strictly above four times the longest
  pause its trace measured**. Strictly above, because four times a D10's 0.0500 s is exactly
  0.200 s and four times a D20's 0.0375 s exactly 0.150 s: landing on the bound leaves nothing for
  the 1/240 s quantisation the trace reports in, which is itself about a tenth of the margin.
- The holds this sets are D6 0.04 s, D20 0.16 s, D8 0.17 s and D10 0.21 s.
- `scripts/fairness-probe.mjs` checks each die against **its own** hold, which the probe program
  prints per die with the trace grid, so the check reads the hold the physics actually ran with.

**This replaces one claim of ADR-0009**: "a result locks when the die stays below 5 mm/s and
0.5 rad/s for 0.15 s", as amended to 0.17 s by slice 0026. Everything else ADR-0009 decided still
stands, including the four-times margin itself, the two rest speeds, the 0.5° cocked tilt and how
all of them are measured. ADR-0009 carries a correction marker at that claim pointing here.

**Why the old reasoning was wrong.** ADR-0009 did not decide that every die should share a hold;
it measured two dice, found one number that covered both, and wrote the number down. The step it
missed is that the quantity being measured, how long a die can balance before it tips, is a fact
about the die's shape and not about the product. Writing it as a product-wide constant made every
later die's measurement a change to every earlier die's behaviour, which is why it has moved twice
in two dice and would have moved again.

## Consequences

- A new die type brings its own measured hold and changes nothing for the dice already shipped.
  The fairness probe's per-die check is what enforces that the number was actually measured.
- The D6 locks 130 ms sooner than it does today and the D20 10 ms sooner; the D8 is unchanged and
  the D10 is the slowest at 0.21 s. This is user-visible and goes in `CHANGELOG.md`.
- Changing any die's hold changes the physics' input, so invariant #2 applies: the three-dice
  fairness test runs before it merges. Lowering the D6's and D20's holds is as much a change as
  raising the D10's, and all four dice are re-tallied.
- A die's hold cannot be chosen by argument from its dihedral angle, tempting as the ordering
  looks. It is measured, and the probe fails a die whose number was not.

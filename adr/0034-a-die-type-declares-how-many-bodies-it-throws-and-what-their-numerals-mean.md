---
id: '0034'
title: "A die type declares how many bodies it throws and what their numerals mean"
type: architecture
status: accepted
date: 2026-11-27
supersedes: []
superseded_by: []
---

## Context

The d% is the last die on the roadmap (`docs/roadmap.md`, v0.2) and the first that is not one
body. A percentile roll is two ten-sided bodies: a tens die reading 00 to 90, a units die reading
0 to 9, scored together as 1 to 100, where 00 and 0 is 100 and not 0. Its geometry is the D10's,
already built and already proven fair (slice 0027), so nothing about the solid is new. What is new
is that one die is two bodies and that a body's numerals no longer mean themselves.

Four shipped assumptions stand in the way. Each was a fact about the dice that happened to exist,
not about dice.

1. **A world has one shape.** `World` holds a single `const DieShape*` for every body
   (`src/physics/world.h`). Two differently numbered dice cannot both live in one world.
2. **Numerals run 1 to N.** `DieShape::faces[].numeral` is 1 to N for every die, checked by
   `test/test_geometry`. A units die's 0 and a tens die's 00 are outside that, and slice 0027
   already refused to number the D10 0 to 9 for the same reason: it would break the one numbering
   rule every die shares and let three dice total 0.
3. **The count is both numbers.** `DiceChoice.count` is how many dice the player asked for and
   how many bodies the world holds. For a d% those differ by two.
4. **A face shows a number.** `ScreenFace::numeral` is a `uint8_t` drawn with `drawNumber`, so
   "00" cannot be drawn at all: it would come out as "0".

Two measurements bound what the answer may cost.

**A roll cannot grow past three bodies.** Raising `MAX_DICE` from 3 to 6, so that three d% could be
thrown, is refused by the frame budget already measured on the device. The Cardputer's worst frame
with three D12s was 15,222 us of the 16,667 a frame has
(`artifacts/roll-verify/cardputer/summary.md`), leaving 1,445 us. Six bodies take dice-to-dice
contacts from 3 pairs to 15 and double the tray contacts, which does not fit in what is left. The
static RAM is the smaller cost and still real: `ContactMemory` is
`tray[MAX_DICE][DIE_MAX_VERTICES][TRAY_PLANE_COUNT]` plus `dice[MAX_DICE][MAX_DICE]` of 16-byte
impulses, so 3 to 6 costs 6,192 bytes there and 156 more in bodies.

**The d%'s own fairness claim is affordable.** Sized by slice 0008's method (99% power at
alpha = 0.001 against one outcome 1.25x too likely, from the noncentral chi-square), 100 outcomes
at 99 degrees of freedom have a critical value of 148.230 and need **162,563 rolls**, taken here
at **163,000** as every other die rounded its own figure up.

The sizing was validated before it was used, and the first attempt at it was wrong. A Patnaik
two-moment approximation returned 161,632, which looked right but sat about 5% under every shipped
size. Summing the noncentral chi-square exactly over its Poisson mixture instead returns 1,821 for
the D4, 3,366 for the D6, 5,083 for the D8, 6,944 for the D10, 8,932 for the D12 and 17,937 for
the D20: the figures slices 0008, 0026, 0027, 0029 and 0032 each recorded before rounding up to
1,900, 3,400, 5,100, 7,000, 9,000 and 18,000. That is the repo's own method reproduced, so 162,563
is the figure and not a floor. Measured cost on this host: 2,000 rolls of two D10s took 1.553 s,
so the full run is about **126 s**.

That matters because the marginals do not imply the joint. Each body's ten faces are the D10's
already-shipped claim; what a d% adds is that the two bodies are independent, and they are not
independent by construction, because they collide in the tray.

## Decision

**A die type declares how many bodies one of it throws**, as `DieKind::bodiesPerRoll`. The
player's count stays a count of dice; the world's body count is `count * bodiesPerRoll`, capped at
`MAX_DICE`. A d% is therefore one per roll and the count control clamps at 1, by the same clamp
`UP` and `DOWN` already have. `MAX_DICE` stays 3.

**A shape keeps its numerals 1 to N, and what a numeral means is declared per body** as
`Numbering`: `Plain`, `PercentileTens` or `PercentileUnits`. The catalog carries one entry per
body a roll throws. The D100's shape is `makeD10`'s, built a second time, so the solid, the
physics and the lock rule are untouched and standing invariant #7's check covers the new type for
free: it declares `NumeralReading::TopFace` because it is a D10.

This is the cheap half of the trade and it was taken deliberately over the honest-looking
alternative of a shape per body. That alternative would let each die really carry its own
numerals, but `World::shape` is read by `stepWorld`, `launchDice`, `drawDice`, the fairness probe
and every test that passes `*world.shape`, and none of those changes buys anything the map does
not: the bodies are the same solid, moving the same way, and only their labels differ.

**Which body is the tens die is fixed by position**: body 0 is tens, body 1 is units. The two are
interchangeable to the physics and tell apart on screen only by colour, so a positional rule is
the only one that needs no state and cannot drift from what the band prints.

**00 and 0 is 100.** A percentile roll is 1 to 100, so the one outcome that would otherwise be
zero is the top of the range. This is the whole reason the value is computed rather than summed.

**A face carries text, not a number.** `ScreenFace` gains a short string, because "00" is two
characters and not the number zero. The datum is already `middle_center`, so a two-character label
centres itself as the D10's "10" already does.

**The d%'s fairness test is over its 100 joint outcomes.** Testing its bodies' ten faces would
re-prove the D10 and say nothing about the pair.

It is also the one die whose goodness of fit runs **launched from rest** rather than chained, and
the reason is measured rather than preferred. A chained roll replays a recorded shake and costs
**7.23 ms** against a launch's **0.78 ms**, so 163,000 chained rolls take **19.6 minutes** a seed
where launches take **2.1**. Both ways put the two bodies in one tray to collide, which is the
whole of what a d% adds to a D10, so the launch tests the new claim at full power. What only
chaining can see is a result following the one before (ADR-0010), and that is checked on its own
at **55,000 rolls**, the sample that catches a previous result 1.25x too likely at 1 in 100 by the
same power and significance.

So `npm test` gains the 163,000-roll launched run, about 2.1 minutes, and `npm run test:fairness`
adds the chained repeat check across three seeds.

## What was rejected, and why

- **Raising `MAX_DICE` to 6** so a player could throw three d%. Refused on the measured frame
  budget above, not on taste: 1,445 us of headroom against a change that quintuples dice-to-dice
  contacts.
- **A `DieShape` per body in `World`.** The most honest model, and the most expensive; see above.
- **Numbering a units die 0 to 9 in the shape itself.** This is slice 0027's rejected option
  arriving a second time. It breaks the one numbering rule every die shares and makes the shape's
  numerals mean different things on different dice, which is exactly what the `Numbering`
  declaration keeps out of the geometry.
- **Making d% a hidden mode on the D10** rather than a type. The mode would be invisible on screen
  until a player already knew it was there, and stepping `PRIMARY` through the types would no
  longer reach every way the device rolls.
- **Proving the d% fair from its marginals plus a cheap dependence check.** Both candidates were
  sized: about 4,240 rolls to catch the two bodies landing alike 1.25x too often, about 8,185 to
  catch the commonest sum 1.25x too likely. Each tests one chosen shape of dependence rather than
  the claim, and at 126 s for the real test the saving does not buy anything.

## Consequences

- The probe's `ShakenTally` counts a die's value where it counted a body's face, so its chained
  repeat check asks a d% whether percentiles repeat and not whether a tens body does. For the six
  one-body dice the two are the same number, and the probe's D10 output is unchanged byte for byte.
- A seventh row joins the catalog and `DIE_TYPE_COUNT` becomes 7. The D100 row holds a second copy
  of the D10's `DieShape`, which is the price of keeping the catalog one flat table.
- `resultsBand` takes each body's text and the roll's total rather than numerals it sums, because
  a d%'s two bodies make one die worth 73 and not two dice worth 70 and 3.
- Standing invariant #8 follows: a type's `count * bodiesPerRoll` never exceeds `MAX_DICE`, every
  body declares a `Numbering`, and a type whose bodies are not all `Plain` ships a fairness test
  over its joint outcomes rather than its bodies' faces.
- Any die added later that is more than one body, or whose numerals do not mean themselves, now
  has somewhere to say so instead of a new special case.

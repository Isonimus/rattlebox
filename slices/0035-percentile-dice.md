---
id: '0035'
title: "Percentile dice"
type: slice
status: accepted
date: 2026-11-27
supersedes: []
superseded_by: []
---

## Goal

Add the d%, the last die the roadmap's v0.2 lists and the first that is not one body. A
percentile roll throws two ten-sided bodies: a tens die reading 00 to 90 and a units die reading 0
to 9, together 1 to 100, where 00 and 0 is 100. Its solid is the D10's, already built and already
proven fair (slice 0027), so this slice adds no geometry. What it adds is a type that throws two
bodies, a declaration of what each body's numerals mean, and a fairness test over the pair rather
than over its halves. The design is recorded in ADR-0034.

No open ledger item is closed by this slice. The `[bug]` line about the fairness probe inferring
its flat-and-propped split from a ratio rather than from which rolls were nudged (ADR-0033) is
untouched: this slice adds a tally, not a trace.

## Definition of Done

- **Given** a device at its boot choice of one D20
- **When** the player presses PRIMARY once
- **Then** the band reads `D100` and the tray holds two dice, the d% being the last type in the cycle

- **Given** a device showing the D100
- **When** the player presses PRIMARY seven times
- **Then** it is showing the D100 again, having stepped through D4, D6, D8, D10, D12 and D20 on the way

- **Given** a device showing the D100
- **When** the player presses SECONDARY, or UP, or DOWN
- **Then** the count stays at 1, because two bodies of three leave no room for a second pair

- **Given** a d% thrown and settled
- **When** every body has locked
- **Then** the band reads the tens body's label, the units body's label and their percentile total, each body's label in that body's own colour

- **Given** a d% whose tens body locks on the face numbered 1 and whose units body locks on the face numbered 1
- **When** the band is built
- **Then** it reads `D100 00 0 = 100`, not `= 0`

- **Given** a d% whose tens body locks on the face numbered 8 and whose units body locks on the face numbered 4
- **When** the band is built
- **Then** it reads `D100 70 3 = 73`

- **Given** a d% at rest on the tray
- **When** the dice are drawn
- **Then** the tens body's visible faces carry two-character labels from 00 to 90 and the units body's carry one character from 0 to 9, and no face carries the shape's own numeral

- **Given** the catalog
- **When** every type is checked
- **Then** each declares a `bodiesPerRoll` of at least one, `bodiesPerRoll` times its largest count never exceeds `MAX_DICE`, and each body it throws declares a `Numbering`

- **Given** 163,000 percentile rolls launched from rest through the real physics
- **When** the 100 outcomes are tallied
- **Then** the chi-square statistic is under 148.230, the 0.1% point at 99 degrees of freedom

- **Given** 55,000 percentile rolls chained one after another on recorded shakes
- **When** the share landing on the value their die showed before is counted
- **Then** it is within 3.2905 standard deviations of 1 in 100

- **Given** both runs across three seeds
- **When** `npm run test:fairness` runs
- **Then** every run passes and none leaves a die unsettled

- **Given** a board on the port
- **When** `npm run verify:roll` rolls the D100
- **Then** every roll locks, the type passes its chi-square, and a capture shows a legible two-character tens label and a one-character units label against a band that agrees with them

## Design

**The type.** A seventh catalog row, `D100`, whose shape is `makeD10(D10_CIRCUMRADIUS_MM)` built a
second time. `DieType` gains `D100` after `D20`, so `PRIMARY` reaches it last and the enum stays in
ascending order of outcomes. `DIE_NAME_MAX_CHARS` was already 4 for exactly this name.

**Bodies per roll.** `DieKind` gains `bodiesPerRoll`. The player's count stays a count of dice and
the world's body count becomes `count * bodiesPerRoll`, so `dice_choice` clamps the count at
`MAX_DICE / bodiesPerRoll`, which is 1 for the D100 and 3 for everything else. `MAX_DICE` stays 3:
ADR-0034 records the measured frame budget that refuses six bodies.

**Numbering.** `DieKind` gains one `Numbering` per body: `Plain` for the six single-body types,
`PercentileTens` then `PercentileUnits` for the D100. The shape's numerals stay 1 to N, so
`test/test_geometry` and standing invariant #7 are untouched and the new type inherits the
reading-rule check for free. A body's label and a die's value are computed from the numbering,
never stored in the shape.

**The value.** `(tens - 1) * 10 + (units - 1)`, and 0 reads as 100. This is the one place the
percentile is not a sum, and it is why the band takes a total from the caller instead of adding
its segments.

**The screen.** `ScreenFace` carries a short label instead of a `uint8_t`, because "00" is two
characters and not zero. The text datum is already `middle_center`, so a two-character label
centres itself; the D10's "10" is the measured precedent that two characters fit a kite face at
product scale.

**Fairness.** The claim is the 100 joint outcomes, not each body's ten faces, because the two
bodies collide and so are not independent by construction. Sized by slice 0008's method, summing
the noncentral chi-square exactly rather than approximating it, and validated by reproducing the
six figures the earlier slices recorded: 162,563 rolls against 148.230 at 99 degrees of freedom,
taken at 163,000 as every other die rounded up.

Unlike every other die, its goodness of fit runs launched from rest rather than chained, on a
measurement: a chained roll replays a recorded shake at 7.23 ms against a launch's 0.78 ms, so
163,000 chained rolls are 19.6 minutes a seed against 2.1. Both ways collide the two bodies in one
tray, which is all a d% adds, so the launch carries the new claim. The repeat check, which only
chaining can make, runs separately at 55,000 rolls: the sample that catches a previous result
1.25x too likely at 1 in 100, at the same power and significance.

**What existing code was considered.** `makeD10` is reused whole rather than copied or
parameterised: a percentile die is a D10, and giving it its own builder would duplicate the kite
geometry for a difference that is not geometric. `kiteFace` and the trapezohedron constants are
therefore untouched. `resultsBand`'s segment layout is reused rather than replaced, because a
d%'s two bodies fit its per-body segments exactly; only what each segment holds changes. The
fairness probe's `tally` mode is extended rather than duplicated, since it already chains rolls
through the real physics and only the thing being counted differs. Deliberately not reused:
`DieShape::faces[].numeral` as the place to put 0 and 00, which slice 0027 already rejected for
the D10 and ADR-0034 rejects again here.

## Verification

- `test/test_die_catalog` covers the catalog scenario: every type's `bodiesPerRoll`, the count
  cap, and a `Numbering` per body.
- `test/test_dice_choice` covers the PRIMARY cycle through seven types and the count clamping at 1
  for the D100.
- `test/test_results_band` covers the band scenarios, including `00 0 = 100` and `70 3 = 73`.
- `test/test_render` covers the label scenario: a tens body's faces carry two characters, a units
  body's one, and neither carries the shape's numeral.
- `test/test_fairness` covers the goodness-of-fit scenario, launched from rest at 163,000 rolls
  against 148.230, and the repeat scenario, chained at 55,000 rolls against 3.2905 standard
  deviations; the repeat half runs with `npm run test:fairness` rather than in `npm test`.
- `npm run test:fairness` (`scripts/fairness-probe.mjs`) covers the three-seed scenario, chained
  and launched from rest.
- `npm run verify:roll` (`scripts/roll-verify.mjs`) covers the device scenario on both boards, and
  writes the captures the last scenario is read from by eye.

## As built

The d% shipped as the seventh catalog row, `D100`, whose solid is `makeD10`'s built a second time.
Every Definition-of-Done scenario was met. Two things did not ship as designed and are recorded
below: a log line this slice had to correct, and a frame cost it could not explain.

**The type and its controls.** `DieType` gained `D100` after `D20`, so `DIE_TYPE_COUNT` is 7 and
`PRIMARY` reaches the d% last. `DieKind` gained `bodiesPerRoll` and one `Numbering` per body, and
the count control clamps at `MAX_DICE / bodiesPerRoll`, which is 1 for the d% and 3 for the rest.
The clamp also runs after a type change, so stepping onto the d% with three dice chosen brings the
count down with no command of its own. On the device the check rolled `1 D100` exactly as the
model predicted.

**The numbering.** The shape keeps its numerals 1 to 10 and `Numbering` says what they mean, so
`test/test_geometry` and standing invariant #7 were untouched and the new type inherited the
reading-rule check for free. `dieValue` scores `(tens - 1) * 10 + (units - 1)`, with 0 reading as
100. The catalog test walks every type's bodies as an odometer and proves the values cover 1 to
the type's outcome count exactly once, which holds for all seven rows.

**The screen.** `ScreenFace` carries a label rather than a `uint8_t`, because "00" is two
characters and not zero. The D10's "10" was the measured precedent that two characters fit a kite
face, and the captures confirm it: the Cardputer shows a tens body reading 50, 90 and 80 with the
band at `D100 90 2 = 92`, and the StickC happened to catch the edge case itself, a tens body on 00
and a units body on 0 against a band of `D100 00 0 = 100`.

**Fairness.** The sizing was got wrong once and corrected before it shipped. A Patnaik two-moment
approximation returned 161,632 rolls, which sat about 5% under every size the earlier slices
recorded. Summing the noncentral chi-square exactly over its Poisson mixture instead reproduces
1,821 for the D4, 3,366 for the D6, 5,083 for the D8, 6,944 for the D10, 8,932 for the D12 and
17,937 for the D20: the repo's own method, which returns **162,563** for 100 outcomes, taken at
163,000.

The goodness of fit runs launched from rest rather than chained, which no other die does. A
chained roll replays a recorded shake at **7.23 ms** against a launch's **0.78 ms**, so 163,000
chained rolls are 19.6 minutes a seed against 2.1. Both ways collide the two bodies inside one
roll, which is all a d% adds to a D10, so the launch carries the new claim at full power. The
repeat check, which only chaining can make, runs separately at 55,000 rolls with the three-dice
set. `npm test` grew from 4m34s to 5m40s, +67 s; the three-dice gate grew to 10m16s.

The probe's `ShakenTally` was generalised from counting a body's face to counting a die's value.
For the six one-body types those are the same number, and the probe's D10 tally came back byte for
byte identical after the change, which is how the generalisation was checked.

**What did not ship as designed, one: the log said two different things.** For one percentile roll
the firmware logged `launch D100 x1` but `shake D100 x2` and `locked D100 x2`, because the launch
line counted the player's dice and the other two counted the tray's bodies. Before the d% those
were always equal, so this slice is what made the log ambiguous and what fixes it: all three lines
now count dice, and the host parser checks `results.length == count * bodies-per-die` and refuses a
type it does not know. A reader could otherwise have seen a roll apparently change size on the way.

**What did not ship as designed, two: an unexplained frame cost.** The slice costs the StickC
**319 to 421 us** of frame work per frame, measured as an A/B of e8090a6 against the slice on one
board in one session; the old commit measured that way reproduced the previous session's figures
within 45 us, so the comparison is sound. The cost is uniform across D4, D6, D8, D10, D12 and D20,
which share no new per-frame code, and that is what makes it odd.

Two causes were ruled out by measurement rather than argument. Formatting a label for every face
was one: `viewDie` now formats only faces that draw a numeral, worth 132 us on the D20 and 123 on
the D12 and nothing on the D4, which is about 5 to 8 us a call and an order of magnitude too
small. `drawString` against the old `drawNumber` was the other, and LovyanGFX's `drawNumber`
formats into a buffer and then calls `drawString`, so the new path does strictly less. The
residual may be instruction-cache layout after the image grew 1,388 bytes of flash and 768 of RAM,
but that was not demonstrated and so is not claimed. It is carried in `LEDGER.md`.

The cost buys time rather than spending it: the worst StickC frame was 11,136 us of the 16,667 a
frame has, leaving 5,531 unused, with no late frames on either board for any type.

**Cost and results.** Cardputer RAM 54,020 B (16.5%) and flash 496,115 B (14.8%); StickC RAM
53,496 B (16.3%) and flash 451,523 B (13.5%). That is +784 and +768 bytes of static RAM over slice
0032, which is the catalog row alone: the shape limits ADR-0030 set were not touched, as it
predicted a d% would not need to.

Both device checks passed with every roll locked and no late frames. The Cardputer's d% scored
chi-square 3.00 (p 0.964) over 80 body results with 7 nudges and 0.46 / 0.97 s a roll; the
StickC's 9.75 (p 0.371) with 6 nudges and 0.47 / 0.87 s. The d% is among the cheapest dice to
simulate, at 3,119 us of physics on the StickC against the D12's 5,983, because it is two bodies
rather than three.

The adversarial pass found no crash or wrong output. Its strongest finding was accepted: a comment
in `die_catalog.h` claimed `test_die_catalog` held every row inside `MAX_DIE_OUTCOMES` when nothing
checked it, so a later row of 11 faces thrown two at a time would have written past the probe's
101-element tallies as silent host stack corruption. The assertion was added and shown to bite. Its
finding that the `SECONDARY` and `UP` bounds are inert today was accepted as fact and rejected as a
change, with the reason written at the site: removing them would stick the count at 2 on a type
holding two dice instead of cycling it to 1.

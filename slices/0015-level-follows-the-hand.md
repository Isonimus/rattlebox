---
id: '0015'
title: "Level follows the hand while dice are in flight, so the reading pose is level"
type: slice
status: accepted
date: 2026-11-21
supersedes: []
superseded_by: []
---

## Goal

Fix the ledger's bug: on the StickC held upright like a phone, the dice slide to the screen's top
edge after a few shakes (operator, 2026-11-21, during slice 0013). The ledger suspected the level
frozen while a roll is in flight (slice 0012), unmeasured. The level probe measured it, after two
wrong turns recorded in ADR-0014: people recline the screen about 24° to read after a shake, and
the frozen level turns that into a slope toward the top. This slice ships ADR-0014's rule: in
flight, the level follows the reading averaged over 0.1 s.

## Definition of Done

- **Given** a roll in flight on a device held at one angle
- **When** the device is held still at another angle for a few tenths of a second
- **Then** the tray's gravity points straight down the glass again, without waiting for the dice
  to lock

- **Given** a roll in flight
- **When** the reading's 0.1 s average falls below 0.1 g, as in free fall
- **Then** the level stays where it was, and nothing aborts

- **Given** every die locked
- **When** the device is held at a new angle
- **Then** the level follows the latest still span, as slice 0012 shipped

- **Given** the three upright captures replayed by the level probe
- **When** the product's rule is compared with the frozen one on the same starts and seeds
- **Then** the share of dice ending against the top wall falls from 61% (D6) and 65% (D20) to within
  a few points of the 41% and 42% of the control

- **Given** the product's shake roll
- **When** the fairness test chains rolls one die at a time (`npm test`) and three at a time
  (`npm run test:fairness`)
- **Then** both checks pass for the D6 and the D20, the loaded D20 still fails chi-square, and the
  throw alone still fails the repeat check

- **Given** the StickC held upright like a phone
- **When** the operator shakes it to roll a few times, then reads the result
- **Then** the dice do not gather at the top edge

## Design

`ReadingAverage` (`src/imu/reading_average.{h,cpp}`) is the exponential average: each sample pulls
it toward the reading by 1 − e^(−Δt/τ), with Δt from the timestamps (invariant #4) and τ = 0.1 s.
The first sample sets it. `ShakeRoll::addSample` feeds it every sample, and the level rule becomes:
every die locked and a still span found, the still span's level; otherwise, once a level exists and
the average holds at least 0.1 g, the average's level. Until the first still span the tray stays
level, as before.

Existing code considered: `StillSpan` keeps a window of samples and a mean, but a window mean
weights the whole window alike and lags by half its length, where an exponential average needs one
vector and reacts at once; the level probe's `low-pass-100` variant was this average and is what
the numbers in ADR-0014 measured. `ShakeDetector` keeps its own window for a different question.
Neither is reused.

The level probe (`src/probes/level/`, env `native-level-probe`) stays: it compares the product's
rule with the frozen one and with the control on any capture of the whole gesture.

## Verification

- `test/test_reading_average`: the first sample, the pull by the timestamps' spacing, a long gap.
- `test/test_shake_roll`: a roll in flight levels to a new hold within a few tenths of a second; the
  level holds through a reading near zero; locked dice still follow the still span.
- The level probe on `artifacts/imu-upright/upright-{1,2,3}.csv` (not committed: operator captures).
- `npm test` and `npm run test:fairness` (invariant #2).
- `npm run verify:shake` on both boards (wired, slice 0012), and the operator's upright shakes on the
  StickC.

## As built

Shipped as designed: `ReadingAverage` (`src/imu/reading_average.{h,cpp}`, τ = 0.1 s), the level rule
in `ShakeRoll::addSample` with `LEVEL_MIN_AVERAGE_G` = 0.1 g, and the level probe
(`src/probes/level/`, env `native-level-probe`) with its product, frozen and swapped-hold variants.

- **Held at a new angle in flight:** met. `test_a_roll_in_flight_follows_the_held_angle_by_its_average`
  checks that one sample moves the level by exactly the average's pull, and that the tray's gravity
  points straight down the glass after 1 s at the new angle while the die is still in flight. It
  failed against slice 0012's frozen rule.
- **Free fall:** met. `test_a_roll_in_flight_keeps_its_level_through_free_fall` feeds 1 s of zero
  reading, then 1 s at half `LEVEL_MIN_AVERAGE_G` in another direction, and checks the level held.
  Its first form read the level from one sample that left the average 5% under the threshold, so
  retuning either constant would have failed it for nothing (adversarial review).
- **Locked dice:** met. `test_locked_dice_level_to_the_still_span_not_the_average`.
- **Upright captures:** met. Replayed by the level probe, 20 rolls of three dice per take:

  | Variant | D6 top / bottom | D20 top / bottom |
  |---|---|---|
  | frozen (slice 0012) | 61% / 2% | 65% / 3% |
  | product | 43% / 6% | 32% / 9% |
  | swapped-hold (control) | 41% / 22% | 42% / 18% |

  On the shake fixtures with `--mean-hold`, D6 top / bottom went from 24% / 34% to 38% / 15% on the
  StickC and from 11% / 35% to 26% / 16% on the Cardputer (ADR-0014, Consequences).
- **Fairness:** met. `npm test` passed, 175 C++ tests, with the one-die fairness test at 328 s;
  `npm run test:fairness` passed its 4 tests in 208 s. The one-die fairness test took 326 s on the
  parent commit too, so this slice did not slow it; slice 0012 recorded 146 s.
- **Tests bite:** four hand mutants of the level rule were each killed by `test_shake_roll`: no
  0.1 g guard, the raw reading in place of the average, the average while dice are locked, and the
  average before any level exists. The last one survived at first and is killed by a new assertion
  that the tray stays level until the first still span.
- **Device:** `npm run verify:shake` passed on the StickC on 2026-11-22: 6 rolls, each detected
  once, no lost IMU samples, no late frames, longest frame work 8.7 ms.
  On the Cardputer it passed the same day: 6 rolls, no lost IMU samples, no late frames, longest
  frame work 12.7 ms; roll 2 was detected twice because the operator shook it twice.
- **Upright on the StickC:** met. The operator shook it upright like a phone and read it; the dice no
  longer gathered at the top edge.

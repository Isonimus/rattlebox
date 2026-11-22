---
id: '0018'
title: "Locked dice keep the level their roll ended on until the device is held still again"
type: slice
status: accepted
date: 2026-11-22
supersedes: []
superseded_by: []
---

## Goal

Close the ledger's bug against ADR-0014: once every die locks, `ShakeRoll` takes the level from the
latest still span, however old. If the hand never holds within 0.1 g for 0.25 s between the shake
and the last lock, that span is the hold from before the shake, so the level snaps back to it and the
resting dice slide. It never happened in 120 replayed upright rolls (dice lock 1.9 s after detection
on average), but nothing stops it: a hand that keeps moving a little while the dice settle is enough.
The adversarial pass found the same snap from a span held while the dice fly, once the hand moves
on to read; the ledger's numbers show that one at every lock in those rolls, at most 8.1 deg and
2.6 deg at the median. ADR-0014 gains an amendment, because its decision is the sentence that allows
both.

## Definition of Done

- **Given** dice resting on a level taken from a still hold
- **When** a shake rolls them and every die locks while the hand has not been still since
- **Then** the level stays the one the roll ended on, not the hold from before the shake

- **Given** dice resting on a level taken from a still hold
- **When** a launch rolls them and every die locks while the hand has not been still since
- **Then** the level stays the one the roll ended on, not the hold from before the launch

- **Given** a roll in flight during which the hand held still for a still span
- **When** the hand moves to another angle, never still there, and every die locks
- **Then** the level stays the one the roll ended on, not the span held in flight

- **Given** a roll whose dice have all locked
- **When** the device is then held still at a new angle for a still span
- **Then** that angle becomes the level, as before

- **Given** the change to the level
- **When** the fairness test chains rolls one die at a time (`npm test`) and three at a time
  (`npm run test:fairness`)
- **Then** both checks pass for the D6 and the D20 (invariant #2)

## Design

`StillSpan::forget()` clears the span found so far, so `found()` is false until the window is still
again. `ShakeRoll::addSample` calls it on the first sample that finds every die locked, so a locked
roll levels only to a span found since its dice locked. The window's samples are kept: a span they go
on to complete ends at the newest sample, so its angle is where the hand is now, even if it began
before the lock.
Until one is found the locked branch does not touch the level, which is the 0.1 s average's at the
last sample of the roll in flight, the angle the hand is reading at.

Existing code considered: the ledger line's own fix, "take only a still span found since the roll
took off", restarting in `kick` and `launch`. It was built first and the adversarial pass broke it:
a span held while the dice fly is found after the take-off, and a hand that then moves to read
before the dice lock snaps the level back to it, the same tilt under resting dice. Restarting at the
lock covers the hold before the roll and every hold in flight with one call. Recording a timestamp
and comparing it with the end of the latest span would do the same with a second piece of state and
a comparison across the µs wrap; clearing the span keeps the rule in the one place that already
knows what a span is. Clearing the window as well was built second and dropped: a `ShakeRoll` made
over resting dice, as the fairness test and the level probe make one each roll, saw the lock on its
first sample and threw that sample away, so the level came 10 ms late, and the chained physics turned
that into different rolls (the probe's D20 top-wall share moved from 30% to 39% with no roll ever
reaching the rule).
Keeping the 0.1 s average running after the lock was not taken: ADR-0014 freezes the level once the
dice rest, so a tilt slides them.

## Verification

- `test/test_shake_roll`: the shake, launch and hold-in-flight scenarios, each failing before the
  fix (the level snapped back to an older span) and passing after; the still span after the lock
  still levels.
- `test/test_still_span`: after `forget()`, a sample that moves the device keeps the span from being
  found, and a still window finds it again at once.
- `npm test` and `npm run test:fairness` (invariant #2).
- Both envs build (invariant #1): `src/roll/` and `src/imu/` are shared.
- The level probe (`native-level-probe`, slice 0015) reruns on the three upright takes before and
  after the fix. It stops at the lock, so its wall shares must not move: the fix changes nothing
  before it.


## As built

`StillSpan::forget()` clears the found span and keeps the window; `ShakeRoll::addSample` calls it on
the first sample that finds every die locked. ADR-0014 gained the amendment. Two designs came
before it, as the Design records: a restart at the take-off, which the adversarial pass broke with a
span held in flight, and a restart at the lock that also cleared the window, which the level probe
caught delaying the level by a sample.

Each Definition of Done scenario, on 2026-11-22:

- **After a shake, after a launch, and after a hold in flight.** The three
  `test_dice_locked_after_*_keep_the_rolls_level_until_a_new_still_span` tests failed before the
  fix, the level's alignment with the roll's 0.966 (cos 15 deg: the tray tilted back the 30 deg to
  an older span), and pass after. The hold-in-flight test also failed against the take-off restart.
- **A still span after the lock.** `test_a_still_span_after_the_lock_levels_the_locked_dice` passed
  before and after: it guards against a fix that stops levelling locked dice at all. The two
  `test_a_forgotten_span_*` tests cover `forget()` on its own.
- **Fairness (invariant #2).** `npm test`: 183 tests passed, the one-die fairness test in 138 s.
  `npm run test:fairness`: 4 tests in 84 s. Both envs built (invariant #1).
- **The level probe.** On the three upright takes its output was identical before and after the
  fix, the product rule ending 39% of D6 and 30% of D20 within a quarter of the depth of the top
  wall, as slice 0017 recorded.

The machine was idle for these timings. The 336 s and 208 s slice 0017 recorded ran alongside a
process holding every core; the one-die test took 131 to 133 s at slice 0012's, slice 0013's and
slice 0017's commits on the idle machine, so no slice slowed it, and the ledger's audit closed.

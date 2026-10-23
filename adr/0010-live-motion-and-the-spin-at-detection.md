---
id: '0010'
title: "The device's own motion moves the dice; each detected shake throws every die and spins it over 0.1 s to a uniformly random orientation, and the fairness test chains replayed shakes and checks repeats as well as chi-square"
type: architecture
status: accepted
date: 2026-10-24
supersedes: []
superseded_by: []
---

## Context

ADR-0002 modelled a shake as an impulse: shake energy would scale how hard the dice were thrown,
and the host test would cover rolls launched from rest and left alone on a level tray. ADR-0009
built that test: 18,000 D20 and 3,400 D6 results, each roll launched from rest at a uniformly
random orientation.

The operator chose instead that the device's own motion drives the dice. The input needs no
sensor fusion. A die in a box that the hand accelerates feels what the accelerometer reads,
negated, so the tray's gravity is that reading rotated into the tray's frame and scaled by
−9810 mm/s². The rotation is the shortest arc that carries the reading taken while the device was
still to +z, so whatever angle the device is held at reads as level. The device frame and the
tray frame are the same (x to the screen's right, y to its top, z out of the glass). The rotating
frame's own terms (centrifugal, Euler and Coriolis, from the gyro) are left out. Leaving them out
stirs the dice less than the real motion would, which is the conservative side for fairness.

Once the dice are moved by real motion, each roll starts from the dice where the last roll left
them, not from a launch. The question becomes whether a shake carries a die away from its
previous result. Slice 0008's probe was extended to measure that (`scripts/fairness-probe.mjs`,
2026-10-24). It replays the twelve shakes the IMU probe recorded at 100 Hz (slice 0004: three
gentle and three hard takes per board, now `test/fixtures/shakes/`) as the tray's gravity on
dice at rest. ADR-0005's detector runs on the samples by their timestamps, and each detection
starts the roll over. Nothing locks while the detector reads a shake. A set time after the first
detection (the cut), the device is held still and level until every die locks.

**The motion alone keeps the previous result.** One die, 1,500 rolls per take, each take from its
own seed, pooled over the six takes of a gesture, cut right at detection. The table gives the
share of results on the face the die showed before the shake (chance: D6 16.67%, D20 5.00%):

| At detection | D6, gentle | D20, gentle | D6, hard | D20, hard |
|---|---|---|---|---|
| nothing | 87.7% (worst take 100%) | 29.7% (65.8%) | 28.1% (47.9%) | 5.0% (5.3%) |
| ADR-0009's throw | 40.0% (45.8%) | 8.1% (12.9%) | | |
| the throw and a jump to a uniform orientation | 16.2% (17.7%) | 5.3% (6.2%) | | |
| the throw and a 0.1 s spin to a uniform orientation | 17.3% (18.5%) | 5.0% (5.7%) | | |

A gentle shake slides a die rather than tumbling it. A throw is not enough: with the shake going
on for 1 s after detection, the D6 still showed its previous face 33.4% of the time.

**Chi-square cannot see this.** Rolls chained one after another, each on a random take cut at a
random time, were run with the throw alone at the fairness test's sizes and seeds. All six D6 runs
passed chi-square (p from 0.001 to 0.89), yet 24.4% to 26.2% of results repeated the die's
previous face. Chi-square counts how often each face comes up, not which face follows which.

**What turns the die.** A jump to a uniformly random orientation shows as a jump, so the
orientation is reached by a spin instead. The spin runs at a constant rate about one axis, set
before each physics step so that the step's contacts meet a die already turning. Each of the
following was measured on intermediate builds of this probe:

- When the turn was applied after the step, dice were driven up to 3.7 mm (D6) and 1.6 mm (D20)
  through the walls; applied before the step, the depth is the 0.01 mm contact slop.
- A die left spinning about the turn's own axis carried on away from its start, and repeated
  less often than chance; it is now left with a spin drawn on its own.
- Counting the turn's elapsed time as a float sum of steps could fall short of its end, so the
  steps are counted.

The spin's length, with the gentle D6, cut at detection:

| Spin | 0.05 s | 0.1 s | 0.2 s | 0.4 s |
|---|---|---|---|---|
| Previous face | 17.6% | 17.3% | 15.8% | 15.1% |
| p against chance, two-sided | 0.020 | 0.113 | 0.022 | < 0.001 |

The share falls as the spin lengthens, from above chance to below it. Why it falls below chance
was not established. 0.1 s is the closest of the four to chance; 0.05 s is not shown worse at
α = 0.001, and a sweep on an earlier build of the spin put it at 16.9% to 17.3%.

**Chained with the 0.1 s spin, the dice are fair on both checks.** One and three dice of each type,
seeds 1 to 3, at ADR-0009's sizes:

- All 12 runs pass chi-square, the lowest p being 0.226.
- The share of results on the previous face is 4.92% to 5.13% for the D20 and 16.06% to 17.15% for
  the D6, the lowest p being 0.342.
- Every roll settled. The deepest any vertex went past a wall was 0.01 mm, and the fastest die
  moved at 5,166 mm/s.
- A roll took a mean of 3.1 to 3.3 s from detection to the last lock, and at most 6.7 s.
- 10% to 12% of rolls were detected more than once: the gentle takes hover near the detector's
  0.23 share. Each re-detection restarts the roll.

With twelve runs sharing the development machine's twelve cores, one D20 at the test's size took
about 130 s and one D6 about 27 s; three dice took about 140 s and 31 s. Run alone, the test is
quicker (see Consequences).

**A planted bias still fails.** The D20 loaded 0.25 mm off its centre of mass, ADR-0009's control,
scored 590 against the critical 43.8 under chained shakes. A D6 loaded as far scored 11.0, which
passes, and at 0.5 mm it scored 28.8 against a critical 20.5.

## Decision

- **The device's motion is the tray's gravity**: `trayGravity(level, accelG)`, with `level` from
  `levelFrom(stillReading)` (`src/physics/tray_gravity.h`). The gyro's rotational terms are left
  out.
- **A shake is detected by ADR-0005's busy share**, on sample timestamps (`src/imu/shake_detector.h`).
  Each detection, the first and any re-detection, starts a new roll: every die's result starts
  over, and the die gets ADR-0009's throw.
- **Each die also spins to a uniformly random orientation over 0.1 s** (`REORIENTATION_S`): at a
  constant rate about one axis, the shorter way round, applied before each physics step. When the
  turn ends, the die is put exactly at the drawn orientation and left with its own spin: 10 to
  30 rad/s about a uniformly random axis (`src/physics/reorientation.h`). The orientation is drawn
  from the RNG and never aimed at a face, so no result is steered (ADR-0002).
- **No result locks while the detector reads a shake.** A die that pauses mid-shake has not
  produced a result.
- **Invariant #2's test** (`test/test_fairness`) rolls dice the way the device does: one roll after
  another on the twelve recorded shakes, each roll on a random take cut at a random time after
  detection, at ADR-0009's sizes with seed 1. It applies two checks at α = 0.001:
  - chi-square goodness of fit of the results;
  - the share of results on the die's previous face, as a two-sided binomial z against 1 in
    faces.

  Two negative controls must fail: the loaded D20 must fail chi-square, and a throw without the
  spin must fail the repeat check.
- **One die at a time runs in `npm test`. Three at a time runs with `npm run test:fairness`**
  (the `native-fairness` env), and must pass before any of the following merges: a change to the
  physics or its constants, the throw, the spin, the shake detector, the tray gravity, or the
  shake fixtures.

This amends ADR-0002 and ADR-0009 at the claims marked in each.

ADR-0002's reasoning was wrong in two ways:

- It took a shake to be an impulse whose only effect is how hard the dice are thrown, so a test of
  launches from rest covered every roll. With the device's motion moving the dice, a roll starts
  from the last roll's result, and the shake can carry the result along with it.
- It took chi-square as the measure of fairness. Chi-square is blind to that carry: the throw
  alone passed it while repeating the last result 24% to 26% of the time.

ADR-0009's launch was fair because it drew a uniformly random orientation for every roll. A shake
does not supply that orientation, so the spin does.

## Consequences

- The fairness test now takes 150 s of `npm test` (one D20, one D6, the loaded D20 and the throw
  alone), up from ADR-0009's 27 s; the whole of `npm test` took 2 min 42 s. Three dice take 88 s
  and run only when `npm run test:fairness` is run by hand: there is no CI to run it (LEDGER).
- The test rests on twelve takes from one operator on one day. A shake unlike them (slower,
  rotational, one-handed) is untested.
- The device's level is taken from a still reading. `docs/roadmap.md` plans to capture it from
  the holding angle at the start of each roll. Which samples count as that angle, given that the
  detector fires about 0.9 s into a shake, is the roll slice's to decide.
- About one roll in nine is detected more than once and gets a second kick partway through the
  shake. Whether that shows as a stutter on screen is the roll slice's to check.
- Each die is driven through its 0.1 s turn, overriding what contacts would do to its orientation.
  The dice still collide and are pushed out of the walls during the turn.
- Retuning the physics, the throw, the spin or the detector reruns both halves of the test. If
  either check fails, ADR-0002's rule still holds: a new ADR decides the fix.
- α = 0.001 applies to each check, not to the set of them. A retune that leaves the dice fair
  still fails one of the merge gate's 8 checks about 0.8% of the time, and one of the probe's
  roughly 30 about 3% of the time. Before deciding that a failure is real, rerun the probe's
  seeds 2 and 3: a fluke does not repeat across seeds, and a real bias does.

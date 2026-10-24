---
id: '0012'
title: "Shake to roll: the device's motion moves the dice, a detected shake throws and spins them, and level is the latest still span"
type: slice
status: accepted
date: 2026-10-24
supersedes: []
superseded_by: []
---

## Goal

Slice 0011 put the dice on the device, launched from rest by the controls. This slice makes the
device's own motion roll them, as ADR-0010 decided: the tray's gravity is the accelerometer's
reading turned by the holding angle, ADR-0005's detector starts a roll on a shake, and each
detection throws every die and spins it over 0.1 s to a uniformly random orientation. It closes
two ledger items:

- **[feature]** "The shake detector, tray gravity and spin at detection are built and host-tested
  but not on the device; the roll slice wires them into the product and captures level from the
  holding angle at the start of each roll, settling which samples count as that angle (ADR-0010)."
  This slice wires them in and settles the samples: the latest still span.
- **[audit]** "About one roll in nine is detected more than once, ... the roll slice checks whether
  that shows as a stutter (ADR-0010)." The device logs every detection, and the operator watches
  the re-detected rolls; the finding lands in As built.

The shake roll moves out of the fairness probe into product code, so the device, the fairness test
and the probe run one implementation, as slice 0011 did for the launch.

## Definition of Done

- **Given** every die locked and the device held still at any angle for 0.25 s
- **When** the tray's gravity is taken from the next sample
- **Then** it points straight into the tray's floor, so resting dice stay put at whatever angle the
  device is held

- **Given** dice at rest and the device held still
- **When** the device is shaken until the detector fires
- **Then** every die is thrown and spins for 0.1 s, every result starts over, no die locks while the
  detector reads a shake, and once the device is still every die locks and the band shows the
  results

- **Given** a roll in flight
- **When** the device is held still at a new angle
- **Then** level stays the angle captured before the shake until every die has locked, and only
  then follows the new angle

- **Given** the product's shake roll, with each recorded shake replayed after 1 s held still
- **When** the fairness test chains rolls one die at a time (`npm test`) and three at a time
  (`npm run test:fairness`)
- **Then** both checks pass for the D6 and the D20, the loaded D20 still fails chi-square, and the
  throw alone still fails the repeat check

- **Given** the product running at 60 fps
- **When** the device is shaken to roll on each board
- **Then** each roll locks, the screen capture at the lock shows each die's face up matching the
  band, no sample is lost beyond the chips' measured 1.1% clock error, and no frame runs late

- **Given** a shake detected more than once
- **When** the operator watches the roll
- **Then** whether the second kick shows as a stutter is recorded, from the device's count of
  detections per roll

- **Given** the controls and the serial port
- **When** a die type or count is changed, or `R` is sent
- **Then** the dice still launch from rest, and `npm run verify:roll` still passes on both boards

## Design

**Level is the mean of the latest still span.** A span is the samples of the last 0.25 s by their
timestamps (invariant #4), and it is still when every sample is within `STILL_SPREAD_G` = 0.1 g of
the span's mean. Measured on the IMU probe's captures (slice 0004) at 100 Hz, the fast-rate takes
cut down to 100 Hz, over every sliding 0.25 s window:

| Gesture | Median spread | 90th percentile | Largest |
|---|---|---|---|
| Rest on the desk, both boards | 0.006 to 0.008 g | 0.010 to 0.144 g | 0.442 g |
| Held still in the hand, both boards | 0.033 to 0.037 g | 0.078 to 0.089 g | 0.216 g |
| Slow tilt, both boards | 0.084 to 0.118 g | 0.122 to 0.216 g | 0.343 g |
| Walking, both boards | 0.197 to 0.253 g | 0.305 to 0.448 g | 0.817 g |
| Inside a shake, the stillest window of each of 24 takes | | | at least 0.836 g |

At 0.1 g, about nine in ten hand-held windows count as still, so a hold yields a level within a
fraction of a second, and the stillest moment inside any recorded shake is more than eight times
over. The StickC's rest takes reach 0.442 g only in windows holding a brief knock, one to a few
samples off the take's median (16, 4 and 1 samples over 0.05 g in the three takes); a knock only
puts off the next still span by 0.25 s.

**Level follows the latest still span whenever every die is locked, and is frozen while a roll is
in flight**, from a detection or a launch until the last die locks. Whatever angle the device is
held at reads as flat, as `docs/roadmap.md` asks, and the shake itself cannot move it: a shake has
no still span, and the detector fires about 0.9 s in, so the span in force is the hold before it.
Until the first still span after boot, the tray stays level.

**The shake roll is product code**: `ShakeRoll` in `src/roll/shake_roll.{h,cpp}`, beside slice
0011's `dice_roll`. It owns the detector, the still span, the level and each die's turn:

- `addSample` feeds a sample to the detector and the still span, sets the tray's gravity and, on
  the detector's rising edge, starts every result over and kicks every die; it says whether it did.
- `step` continues each die's turn, steps the world and, unless the detector reads a shake,
  advances the results.
- `launch` is slice 0011's launch from rest, also cancelling any turn in progress, which would
  otherwise override the launch's orientation.

The kick is a `KickPlan`, as in the probe: the product passes the 0.1 s spin, and the fairness
test's negative control passes the throw alone. Existing code considered: `simulateShakenRoll`
(`src/probes/fairness/shaken_roll.cpp`) holds this logic today, interleaved with the replay. Its
per-sample and per-step halves move into `ShakeRoll`. The replay, the tallies and the recording
loader stay in the probe, which is host-only. The still span is new: no code computes a windowed
spread. `ShakeDetector`'s timestamp window is the model for it, not reused, since the detector keeps
only a busy flag per sample and the span needs the readings.

**The fairness test replays each shake after 1 s held still.** 11 of the 12 fixtures start
mid-shake, so the product's capture would find no still span in them. In the one take with a
still lead-in (`cardputer-shake-gentle-1`), the last still span before detection ended 1.47 s
before it and its level was 2.8° from the recording's mean, the level the test used until now. The
hold is at the recording's mean, so the test's level does not change. The detector now sees still
samples before the shake, so it fires partway into the shake instead of the instant its 1 s window
fills, as on the device. The dice rest through the hold, so it is fed to `ShakeRoll` before the
first physics step rather than stepped: stepping resting dice changes nothing and would add a
second to every replayed roll. This changes the test's input, so both halves of the fairness test
rerun (invariant #2).

**The IMU is read inside the frame loop.** It is polled before each of the frame's four physics
steps and throughout the frame's pacing wait, every `IMU_POLL_US`; each new sample goes to
`ShakeRoll`. A poll only between frames would lose about 40% of samples at 60 fps against 100 Hz.
Slice 0011's frames peaked at 12 ms of work, about 4.6 ms of it physics, so the gaps between polls
should stay under the 10 ms sample period. A sample's timestamp is its read time (`micros()` in the
drivers), off by at most the gap between polls, which the 1 s detector window and the 0.25 s span
absorb. A bus error halts the device on screen: nothing it would show afterwards could be trusted.
Each roll's `[frame]` line adds the samples read, the seconds they spanned and the longest gap
between them, so a loss is measured rather than assumed. A dedicated IMU task on the other core was
considered and not chosen: it adds a queue and cross-core state for a margin this loop is expected
to have, and the count above shows whether it does.

**The log.** A detection prints `[roll] shake <type> x<count> level_tilt_deg=<angle>`, the captured
level's tilt from the glass's normal, and the locked line adds `detections=<n>`. `scripts/shake-verify.mjs`
reads them.

## Verification

- `test/test_shake_roll` (`npm test`), on synthetic samples:
  - a still span sets the level, so the tray's gravity points straight down at a tilted hold, and a
    span over `STILL_SPREAD_G` does not;
  - a shake's rising edge starts every result over and kicks every die, once per edge;
  - no die locks while the detector reads a shake;
  - level is frozen while a roll is in flight and follows the latest still span once every die locks;
  - before any still span, the tray stays level;
  - a launch cancels a turn in progress.
- `test/test_still_span` (`npm test`): the span's timestamp window, its mean and its spread, and the
  first span only once the window is full.
- `test/test_fairness` (`npm test` one die, `npm run test:fairness` three dice), now on `ShakeRoll`
  with the 1 s hold before each replay; the negative controls still fail.
- `scripts/shake-verify.mjs`, wired as `npm run verify:shake`: interactive, run by the operator on
  each board. It flashes the product, sets three D20 and then three D6, and prompts for three shakes
  of each. It fails on an error line, a roll that never locks or a late frame, and on an IMU sample
  rate below 98% of the configured 100 Hz (the chips' clocks were measured up to 1.1% off it,
  ADR-0005). The rate is pooled over the rolls: a timestamp is a read time, a few milliseconds late
  at most, and over one short roll that alone moves the rate by about 1%. `verify:roll` checks the
  same pooled rate for each die type. It saves the screen capture at each lock and a summary of the
  detections, the level tilts and the frame and IMU figures under `artifacts/shake-verify/<env>/`.
  The captures and the re-detected rolls are for the operator's eyes. Its parsing and checks have
  unit tests in `scripts/shake-verify.test.mjs`, which CI runs.
- `npm run verify:roll` on both boards, for the controls' launch from rest, now also checking the
  pooled IMU rate.

## As built

Shipped as designed: `StillSpan` (`src/imu/still_span.{h,cpp}`), `ShakeRoll`
(`src/roll/shake_roll.{h,cpp}`), the fairness probe's replay on `ShakeRoll` with the 1 s hold
before each shake, and the product reading the IMU before each physics step, through the DMA wait
and through the pacing wait. `ShakeKick` and `KickPlan` moved from the probe into `shake_roll.h`.
Each Definition of Done scenario was met:

- **Level at any held angle, frozen through a roll, level until the first still span, a shake
  starting one roll per rising edge, no lock mid-shake, a launch cancelling a spin:**
  `test/test_shake_roll` (8 tests) and `test/test_still_span` (7 tests) pass. Each behaviour was
  broken on purpose in turn (the in-flight freeze, the mid-shake lock guard, the launch's turn
  reset, the no-level guard, the rising edge, the whole-span guard, the spread bound, the span
  length) and every break failed a test; doubling the span overflowed its buffer and aborted.
- **Fairness:** `npm test`'s one-die half passes, the loaded D20 and the throw alone still failing,
  with the fairness suite taking 146 s run alone, against slice 0011's 157 s: the hold is fed
  without stepping the world. `npm run test:fairness` passes all four three-dice tests in 87 s.
  The whole of `npm test` took 2 min 41 s: 98 script tests, 32 Python tests, 152 host test cases.
- **Shaken on the device** (`npm run verify:shake`), three D20 then three D6 on each board, every
  roll locked with each die's face up matching the band in the captures, and no late frame:

  | Board | Level tilt, degrees | Seconds to lock | Detections | IMU Hz, per roll | Longest frame work |
  |---|---|---|---|---|---|
  | StickC | 20.8 to 31.9 | 1.88 to 2.52 | 1 each | 99.9 to 100.0 | 7.2 ms |
  | Cardputer | 9.4 to 31.5 | 1.23 to 3.08 | 1, one roll 2 | 98.9 to 99.4 | 13.1 ms |

  The operator found every roll on both boards looked right.
- **Stutter:** one roll of the twelve, a D6 roll on the Cardputer, was detected twice. The operator,
  who shook it gently to provoke a re-detection, saw one continuous tumble and nothing strange on
  screen. The second kick lands while the dice are already tumbling from the first, so it reads as
  more of the same.
- **Launch from rest** (`npm run verify:roll`): both boards pass chi-square (StickC D6 p 0.731,
  D20 p 0.478; Cardputer D6 p 0.480, D20 p 0.143) with no late frame. The IMU's rate over all
  rolls was 99.7 to 99.8 Hz on the StickC and 99.4 to 99.5 Hz on the Cardputer.

Deviations and findings:

- **Printing a roll's report stalled the frame loop.** The first `verify:roll` on the StickC had 20
  late D6 frames and 18 late D20 frames, where slice 0011 had none. Instrumenting the loop put it on
  the report, not the IMU reads: the frame after each lock started about 21.8 ms after its deadline.
  This slice's longer lines overran the StickC's 128-byte UART FIFO, and `verify:roll` sends its
  next command the moment the report arrives, so the next roll's first frame started late. `Serial`
  now has a 1,024-byte transmit buffer, set before `begin()`, and aborts if it cannot get one. Late
  frames went to 0 and the StickC's longest frame work from 14.2 ms to 7.4 ms.
- **The DMA wait spins rather than sleeps.** It first slept 1 ms between IMU reads, so it saw the
  push's end up to a sleep and a read late. It now spins on `dmaBusy()`, reading the IMU at most
  every `IMU_POLL_US`.
- **One IMU read takes up to 1.27 ms on the StickC** (data-ready check, burst read and
  `addSample`), at 400 kHz; measured by the same instrumentation and left as it is, since the frame
  budget absorbs it.
- **The IMU rate is checked pooled over the rolls, not per roll.** On the Cardputer, whose BMI270
  ran 1.1% slow in ADR-0005, one 0.6 s launch roll measured as low as 98.5 Hz with no sample lost:
  a timestamp is the read time, up to about 7 ms late, and one late first or last read moves a short
  roll's rate by about 1%. The worst gap between reads was 17.2 ms, under the 20 ms a missed sample
  would leave. The per-roll check was replaced before it failed, with a test showing a late read
  failing it alone and passing pooled; the per-roll rate stays in the tables for reading.
- **A gap in the samples restarts the detector's and the still span's windows.** The wrap-up's
  review found that after the loop stopped reading the IMU (the screen capture holds it for
  seconds), the still span took the one sample after the gap as a whole still span, so a reading
  taken mid-handling could become the level. `ShakeDetector` had the same flaw, which the review
  missed: one busy sample after a gap was a busy share of 1 and read as a shake. Both now restart
  their window on a gap of `IMU_SAMPLE_GAP_LIMIT_US`, 50 ms (`src/imu/imu_sample.h`), with a
  regression test each that failed before the fix. The shake fixtures' largest gap is 10.2 ms, so
  the fairness replay is unchanged; both halves of the fairness test were rerun on the fix. The
  review's other finding, that `setTxBufferSize` might return another size and abort the boot, was
  rejected: both boards' cores return the size asked for on success, which the comment at the call
  now says.
- **The Cardputer's mean frame work is 11 ms against the StickC's 5.3 ms**, with the same physics
  cost: its frames wait longer on the previous push. Its longest, 14.2 ms in `verify:roll`, leaves
  2.5 ms of the 16.7 ms frame.

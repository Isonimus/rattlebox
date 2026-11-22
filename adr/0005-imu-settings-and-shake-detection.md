---
id: '0005'
title: "IMU at ±16 g, ±2000 °/s and 100 Hz on both boards; a shake is detected by how long the motion lasts, not how large it is"
type: architecture
status: amended
date: 2026-10-23
supersedes: []
superseded_by: []
---

## Context

ADR-0001 configured both IMUs for at least ±8 g and left the exact range, output rate and
filtering to an IMU probe. A roll starts on a shake and captures its "level" reference then
(`docs/roadmap.md`, v0.1), so the firmware has to tell a deliberate shake from the device being
handled: a detector that fires on handling starts rolls nobody asked for, and one that misses a
gentle shake ignores the player. Slice 0003 had already shown ±8 g is not enough to measure
with: a hard StickC shake read 8.0 g, the top of the range.

The probe (slice 0004) recorded 5 s windows on each board at ±16 g and the chip's fastest
useful rate (Cardputer ADV BMI270 at 800 Hz, StickC Plus 2 MPU6886 at 1 kHz), three takes of
each of nine gestures by one operator on 2026-10-23: seven kinds of handling (`rest`, `hold`,
`pickup`, `setdown`, `walk`, `tilt`, `bump`, the last an accidental knock against the desk
edge) and two shakes (`shake-gentle`, the softest shake meant as a roll, and `shake-hard`).
Lower rates were simulated from those captures, then the chosen rate was confirmed by capturing
the closest gestures again with the chips set to it.

**Range.** Hard shakes peaked at 6.8 to 9.8 g on the StickC and 5.8 to 9.6 g on the Cardputer,
so ±8 g clips them. At ±16 g no shake saturated any axis; across all 90 takes one sample of
one accidental knock reached the end of the range. Angular rate peaked at 1100 °/s (a knock)
and 963 °/s (a hard shake), inside ±2000 °/s.

**Size cannot tell a shake from a knock.** Every feature that measures how large the motion
is failed on both boards, at every rate, because one knock out-measured the gentlest shake:

| At 100 Hz, chip set to it | Worst shake | Worst handling |
|---|---|---|
| Peak of \|\|a\| − 1 g\|, Cardputer / StickC | 1.30 / 1.55 g | 5.28 / 15.59 g (knocks) |
| Largest 200 ms RMS of \|a\| − 1 g | 0.66 / 0.96 g | 1.31 / 3.53 g (knocks) |
| Peak \|ω\| | 224 / 209 °/s | 416 / 509 °/s (a pick-up, a knock) |

**Duration can.** A knock is one large impulse over in a few hundredths of a second; a shake
keeps going. The feature that separated every take is the **busy share**: the largest share of
any 1 s window whose samples are more than 0.5 g off 1 g. It was picked from a small sweep
(0.3 or 0.5 g, 0.5 or 1 s) over the StickC's takes, then fixed before the Cardputer's takes,
which therefore tested it rather than fitted it:

| Busy share | Worst shake | Worst handling |
|---|---|---|
| StickC, 1 kHz capture (where it was picked) | 0.39 | 0.09 (`pickup`) |
| Cardputer, 800 Hz capture (held out) | 0.33 | 0.11 (`pickup`) |
| Cardputer, chip at 100 Hz | 0.30 | 0.16 (`pickup`) |
| StickC, chip at 100 Hz | 0.58 | 0.14 (`bump`) |

**Rate.** Simulated by averaging blocks of samples, the busy share hardly moved between the
capture rate and 100 Hz (StickC 0.39 / 0.09 to 0.41 / 0.09; Cardputer 0.33 / 0.11 to 0.34 /
0.10), nor when simulated as keeping every Nth sample, which is how the MPU6886 lowers its rate
(0.39 to 0.42 / 0.10). The chips set to 100 Hz confirmed it (table above).

**Clocks.** Each chip's real rate differs from its nominal one: the MPU6886 ran at 999.5 Hz
(0.05% slow) and 100.0 Hz; the BMI270 at 791.5 Hz and 98.9 Hz (1.1% slow), in every take.

## Decision

- **Both boards configure their IMU for ±16 g, ±2000 °/s and 100 Hz output**, through the
  `ImuSettings` each driver translates into its own registers. Filtering stays as slice 0004
  left it (BMI270 normal bandwidth in performance mode; MPU6886 176 Hz gyro and 218 Hz
  accelerometer low-pass), since the detector did not depend on it.
- **A shake is detected by its busy share**: a roll starts when more than **0.23** of the last
  1 s of samples are more than **0.5 g** off 1 g. 0.23 is the midpoint between the worst shake
  (0.30) and the worst handling (0.16) measured with the chips at 100 Hz, leaving 0.07 on each
  side.

  *Amended 2026-11-22 by ADR-0024: over three people's takes, other people's pick-ups and
  set-downs reached 0.27, so the threshold is 0.275; a brisk walk cannot be told from a gentle
  shake, and a slow rocking shake is not a roll.*
- **Timing derived from IMU samples uses their timestamps, never the nominal rate**, which on
  the BMI270 is 1.1% off.

100 Hz is the lowest rate the probe tried and enough for detection. If the physics later needs
a faster input, that is a new ADR superseding this one's rate, with its own measurement.

## Consequences

- The skeleton's ±8 g floor in `scripts/skeleton-verify.mjs` rises to ±16 g, so the product
  firmware cannot drift back under the range shakes need.
- A knock or a firm set-down no longer clips but reads up to 16 g per axis, so physics fed
  raw acceleration must expect impulses far larger than any shake.
- The detector's margin rests on 3 takes per gesture from one operator; a brisker pick-up than
  any recorded, or a shorter, softer shake, could cross it. The probe
  (`node scripts/imu-probe.mjs`) re-measures it on any board.
- The detector itself is not built here: it belongs to the roll slice that consumes it.
- Using timestamps rather than the nominal rate is a rule for all later code, so it is a
  standing invariant in `CLAUDE.md`.

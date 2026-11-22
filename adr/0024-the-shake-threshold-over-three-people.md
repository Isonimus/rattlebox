---
id: '0024'
title: "The shake threshold rises to 0.275 over three people's takes; a brisk walk is not told from a gentle shake, and a slow rocking shake is not a roll"
type: architecture
status: accepted
date: 2026-11-22
supersedes: []
superseded_by: []
---

## Context

ADR-0005 starts a roll when more than 0.23 of the last second's samples are over 0.5 g off 1 g.
It set 0.23 as the midpoint between the gentlest shake (0.30) and the busiest handling (0.16) at
100 Hz, on 3 takes per gesture from one operator. ADR-0010's fairness test rolls on twelve shakes
from that same operator on that same day. The ledger held both as audits to run before a release.

Slice 0023 recorded three people on 2026-11-22 with the chips at 100 Hz: the operator again (`p1`)
and two others (`p2`, `p3`), on both boards. Each recorded three takes of nine gestures:

- the four handling gestures nearest the threshold in ADR-0005 (`pickup`, `setdown`, `walk`,
  `bump`);
- the two shakes recorded before (`shake-gentle`, `shake-hard`);
- three shake kinds nobody had recorded: `shake-slow` (a slow rocking roll), `shake-twist` (the
  wrist turned back and forth) and `shake-one-hand`.

That is 162 new takes. With the 36 the operator recorded on 2026-10-23 with the chips at 100 Hz
(slice 0004's confirmation: both shakes, `pickup`, `setdown`, `walk` and `bump`), 198 in all.

**Busy share** over every take at 100 Hz, lowest to highest (the 2026-10-23 takes in brackets
where they differ):

| Gesture | Cardputer | StickC |
|---|---|---|
| `shake-hard` | 0.78 to 1.00 | 0.81 to 1.00 |
| `shake-one-hand` | 0.80 to 0.98 | 0.74 to 0.92 |
| `shake-twist` | 0.64 to 0.75 | 0.49 to 1.00 |
| `shake-gentle` | 0.30 to 0.81 | 0.28 to 0.85 (0.58 to 0.85) |
| `shake-slow` | 0.19 to 0.36 | 0.00 to 0.70 |
| `walk` | 0.00 to 0.61 (0.00 to 0.00) | 0.00 to 0.62 (0.00 to 0.04) |
| `setdown` | 0.00 to 0.27 (0.00 to 0.01) | 0.02 to 0.26 (0.02 to 0.12) |
| `pickup` | 0.02 to 0.23 (0.02 to 0.16) | 0.02 to 0.26 (0.07 to 0.09) |
| `bump` | 0.02 to 0.09 | 0.03 to 0.17 |

**0.23 fails.** At 0.23, 6 of 48 pick-ups and set-downs would start a roll, so picking the device
up to read a result could roll it again. 13 of the 24 walks would too, and 6 of the 18 slow shakes
would be missed. The walks are what changed most. Every walk on 2026-10-23 stayed at or under
0.04. On 2026-11-22, 13 of 18 walks crossed 0.23, the operator's own among them (up to 0.61), and
peaked at 1.6 to 2.8 g, as hard as a gentle shake (1.8 to 3.2 g). How briskly someone walks
decides it, not whose hands the device is in.

**No feature tried separates a walk from a gentle shake.** The sweep covered, on both boards,
with and without the slow shakes:

- the busy share at 0.3, 0.5, 0.75 and 1 g over 0.5, 1 and 2 s windows;
- the share of samples turning faster than 100 to 300 °/s;
- both together;
- the busiest second's mean rotation rate.

All 50 left a negative margin, the best −0.08. The walk's own rhythm does not help either.
Measured as the strongest autocorrelation lag of |a| over each take's busiest 2 s:

- walks ran at 1.1 to 3.7 Hz;
- gentle shakes at 2.7 to 7.1 Hz;
- twist and slow shakes at 1.4 to 5.6 Hz, inside the walking range.

**Each threshold trades one error for another** (errors over all 198 takes, both boards):

| Threshold | Gentle shakes missed | Slow shakes missed | Walks rolled | Pick-ups and set-downs rolled |
|---|---|---|---|---|
| 0.23 | 0 of 24 | 6 of 18 | 13 of 24 | 6 of 48 |
| 0.275 | 0 of 24 | 9 of 18 | 13 of 24 | 0 of 48 |
| 0.35 | 3 of 24 | 11 of 18 | 8 of 24 | 0 of 48 |

Hard, one-handed and twist shakes were detected at every one, and no bump crossed any.

## Decision

- **A roll starts when more than 0.275 of the last second's samples are over 0.5 g off 1 g**
  (`SHAKE_BUSY_SHARE`). The deviation, the window and the timestamps stay as ADR-0005 has them.
  0.275 lies between the busiest handling take (0.27, a set-down) and the gentlest shake (0.28). At
  100 Hz a second holds about 100 samples, so 27 busy samples do not start a roll and 28 do, and no
  take can fall on the threshold itself.
- **The margin is one sample in a hundred on each side**, against ADR-0005's 0.07. That is what
  three people's takes leave, and it is the narrowest margin that still keeps both errors that
  matter at zero: no gentle shake missed, no pick-up or set-down rolled.
- **Walking with the device can start a roll.** The detector cannot tell a brisk walk from a gentle
  shake. Telling them apart needs another trigger, such as a shake that must start from holding
  still or a button held while shaking. That needs its own probe protocol (walk, stop, shake) and
  is a ledger item, not part of v0.1.
- **A slow, rocking shake is not a roll.** Half of them (9 of 18) never go 0.5 g off 1 g often
  enough, at any threshold that leaves gentle shakes alone. The README tells people to shake
  briskly.
- **The fairness fixtures are every shake the device would roll on**: the operator's twelve and the
  81 of the 90 new shake takes over 0.275 (`imu-probe.mjs --export-fixtures` skips the other nine,
  all slow shakes). Only a detected shake makes a roll, so a missed one tests nothing, and the
  fairness test's check that every fixture is detected keeps the C++ detector and the probe's copy
  of the threshold from drifting apart.

This amends ADR-0005 at the claim marked there. ADR-0005's 0.23 was right for the takes it had.
What it lacked was other people: three takes of one operator's handling under-measured how hard
others pick up and set down, and a calm walk stood in for a brisk one.

## Consequences

- `test/test_shake_detector` holds the threshold between 27 and 28 busy samples in a hundred, the
  measured busiest handling and gentlest shake.
- The fairness test and the fairness probe roll on 93 takes. The probe replays every participant
  take with the product's kick, cut at detection, pooled per kind of shake (slice 0023 has the
  results).
- A pick-up 0.01 busier than any recorded, or a gentle shake 0.01 softer, crosses the threshold.
  The probe re-measures it with `--participant`, and more people's takes make the threshold more
  trustworthy than any argument about it can.
- Walking rolls, and a slow rocking shake does not: both are said in the README, and a walk-proof
  trigger is a ledger item.

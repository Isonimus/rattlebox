---
id: '0014'
title: "While a roll is in flight, the level follows the reading averaged over 0.1 s, not the hold before the shake"
type: architecture
status: accepted
date: 2026-11-21
supersedes: []
superseded_by: []
---

## Context

Slice 0012 took the level from the latest still span while every die is locked and **froze it while
a roll is in flight**, from a detection or a launch until the last die locks. Its reasoning was
that a shake has no still span, so the span in force is the hold before it, and "a hold after the
shake at another angle tilts the tray, as it would a real one, until the last die locks". It
assumed people return to about the angle they shook from.

They do not. On the StickC held upright like a phone, the operator reported the dice sliding to the
screen's top edge after a few shakes (slice 0013). Three captures of the whole gesture (hold
upright, shake, hold to read; `m5stickc-plus2-imu-probe-100hz`, 100 Hz, 5 s each) replayed through
`ShakeRoll` by the level probe (`src/probes/level/`, 20 rolls of three dice per take on the same
starts and seeds per variant) show:

- The hand stops 1.6 to 2.4 s into the take, and the operator then reclines the screen to read it.
  In take 2 the hold before the shake reads (0.02, 0.89, 0.40) g and the hold after (0.10, 0.67,
  0.74) g: about 24° apart, a 0.4 g slope toward the top under the frozen level.
- The detector goes on reading a shake for about 0.7 s after the hand stops, its window being 1 s,
  and the dice slide from 0.45 to 0.84 of the tray's depth in the first 0.25 s of that.
- Frozen, 61% of D6 and 65% of D20 end within a quarter of the depth of the top wall, 2 to 3% of
  the bottom one.

A control replaced every reading from the moment the hand stops with the hold before the shake,
so no hold after could differ from the level: 41% (D6) and 42% (D20) at the top, 22% and 18% at the
bottom. That is the best any level rule can do on these takes; the rest of the lean toward the top
comes from the shake itself.

Rules that were measured and rejected:

| Rule | D6 / D20 at the top | Why not |
|---|---|---|
| Follow the latest still span in flight too | 59% / 63% | A still span needs 0.25 s; the dice have slid by then |
| Remove ω×(ω×r) with the gyro, pivot 0.15 m below the sensor | 58% / 63% | The fitted pivot (0.13 to 0.19 m, correlation 0.62 to 0.89) is real, but its pull does not carry the dice; it also pushed the fixtures' dice to the bottom (StickC D6 bottom 34% to 48%) |
| While shaking, the hold plus the reading's departure from its 1 s mean | 76% / 74% | Worse: the 1 s mean lags the move to the reading pose |
| Follow the last 0.1 s once every reading in it is within 0.3 g of 1 g | 49% / 43% | A gentle rocking shake looks the same: the Cardputer's `shake-gentle-1` stays within 0.1 g of 1 g for 546 ms, and it moved the Cardputer fixtures' dice (D6 top 11% to 24%) |
| Follow the reading averaged with a 0.25 s time constant | 59% / 49% | Too slow for the slide |
| **Follow the reading averaged with a 0.1 s time constant** | **41% / 39%** | Chosen; as built in `ShakeRoll`, 43% / 32% |

## Decision

**While a roll is in flight, the level follows the device's reading averaged with a time constant
of 0.1 s**; while every die is locked it follows the latest still span, as before. A tilt slower
than about 0.1 s carries the level with it, so the reading pose a hand settles into is level by the
time the dice lie still; the shake's own strokes, a few per second, still move the dice.

The average is exponential, weighted by the time between the samples' timestamps (invariant #4),
and runs on every sample, so a roll starts from the angle the device was just held at. Below 0.1 g
it has no direction to level by (a device thrown into free fall reads zero) and the level stays
where it was; the smallest average over all 39 captures at 100 Hz was 0.165 g, in the Cardputer's
`shake-hard-3`.

## Consequences

- A tilt while dice are in flight no longer slides them unless it is quicker than about 0.1 s.
  Slice 0012's "as a real tray would" is given up for the angle a person actually reads at.
- The shake moves the dice differently. On the fixtures replayed with their mean as both holds, as
  built, the StickC's D6 went from 24% top and 34% bottom to 38% and 15%, the Cardputer's from 11%
  and 35% to 26% and 16%; the D20s alike. Invariant #2 applies: both halves of the fairness test
  ran before this merged (slice 0015).
- The fixtures start mid-shake and stand their mean in for the holds, so they cannot show a hold
  after the shake that differs from the one before; the upright takes can. They stay in
  `artifacts/`, and the level probe is how to measure a change to this rule again.
- The leftover lean toward the top on an upright wrist shake (41% top, 22% bottom in the control)
  is the shake's, not the level's, and no level rule removes it.

## Amendment, 2026-11-22: locked dice level only to a still span found since they locked

"While every die is locked it follows the latest still span" let the level go back to a span the
hand had since left: the hold from before the shake when the hand never held still for 0.25 s
between the shake and the last lock, or a hold during the flight when the hand then moved to read.
At the lock the tray tilted under dice already at rest, by as much as the hand had moved since, and
they slid. In the 120 replayed upright rolls the snap to the hold before the shake never happened,
but the jump to a hold in flight came at every lock: at most 8.1 deg, 2.6 deg at the median, against
the 24 deg recline. While every die is locked, the level is the latest still span
found since the dice locked; until there is one, it stays where the roll in flight left it
(slice 0018).

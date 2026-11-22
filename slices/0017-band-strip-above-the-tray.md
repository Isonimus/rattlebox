---
id: '0017'
title: "The band gets its own strip above a shorter tray, and shows the die type before a roll"
type: slice
status: accepted
date: 2026-11-22
supersedes: []
superseded_by: []
---

## Goal

Close the ledger's feature: the 20 px results band is drawn over the top of the tray, so a die
resting against the top wall shows part of its face under it, on both boards, and since tilting
slides dice to the walls it happens often. ADR-0016 decides where the band goes: its own strip at
the top of the screen, with the tray shortened to the 115 px beneath it. The operator asked that
the strip show the die type before the first roll, so it is never an empty bar: the type always,
and the results once every die locks.

## Definition of Done

- **Given** the tray laid out under the strip
- **When** a point of the tray is placed on the screen
- **Then** the tray's top wall lands on the strip's bottom edge, its floor's near wall on the
  screen's bottom edge, and the scale is still ADR-0007's 24 px D20 edge

- **Given** a roll in flight, the one launched at boot included
- **When** the frame is drawn
- **Then** the strip shows the die type alone, and once every die locks it shows the type, each
  die's result and, for more than one die, the total

- **Given** a screen whose size is not the 240 × 135 px the tray is laid out for
- **When** the product boots
- **Then** it stops at the canvas step with the reason on the screen and the serial port

- **Given** the shallower tray
- **When** the fairness test chains rolls one die at a time (`npm test`) and three at a time
  (`npm run test:fairness`)
- **Then** both checks pass for the D6 and the D20 (invariant #2)

- **Given** the product on each board
- **When** dice are rolled until they lock, against the top wall among other places
- **Then** every roll locks, the results pass chi-square, and the captures show no die under the band

## Design

The screen's size, the strip's height and the scale move into `src/physics/physics_config.h`,
which already sized the tray from the screen in a comment: `TRAY_DEPTH_MM` becomes
`(SCREEN_HEIGHT_PX - BAND_HEIGHT_PX) / TRAY_PX_PER_MM`, 50.3125 mm, so the strip and the tray
cannot drift apart. `src/main.cpp`'s `PX_PER_MM` and `band_drawing.cpp`'s `BAND_HEIGHT_PX` were the
two places that held those numbers; both now read the shared constants.

`trayToScreenPx` (`src/render/tray_layout.{h,cpp}`) places a point of the tray on the screen: it is
the mapping `drawDice` did inline, measured from the screen's bottom edge as before, now pure and
tested. `typeBand` (`src/render/results_band.{h,cpp}`) is the strip with the type alone, and
`resultsBand` starts from it. `drawFrame` draws the strip every frame, `resultsBand` once the roll is
reported and every die locked, `typeBand` otherwise.

`FrameCanvas::begin` fails when the panel is not 240 × 135 px, so a board with another screen stops
at boot instead of drawing a tray whose walls are not where the physics puts them.

Existing code considered: `drawResultsBand` already fills a full-width strip and draws segments
from a `ResultsBand`, so the type-only strip is a `ResultsBand` with one segment, drawn by the same
function; no second drawing path. `viewDie`'s orthographic projection already puts a die's
vertices where its body is, so a die inside the tray's box is drawn inside its rectangle with no
clipping needed. The physics test's check that no vertex leaves through a wall covers the box.

## Verification

- `test/test_render`: `trayToScreenPx` puts the top wall on the strip's bottom edge, the near wall on
  the screen's bottom edge, and the full width across the screen's.
- `test/test_results_band`: the type-only strip, and the results strips as before.
- `test/test_physics` (unchanged): no vertex leaves the tray's box, now 50.3 mm deep.
- `npm test` and `npm run test:fairness` (invariant #2).
- Both envs build (invariant #1).
- `npm run verify:roll` on each board (wired, slice 0011): every roll locks, chi-square passes, and
  its captures are read for dice under the band. The screen-size check is read in the code: no
  board with another screen exists to boot it on.
- The operator watches a roll on the device: the type in the strip while the dice fly, the results
  after.

## As built

Shipped as designed. `SCREEN_WIDTH_PX`, `SCREEN_HEIGHT_PX`, `BAND_HEIGHT_PX` and `TRAY_PX_PER_MM`
live in `src/physics/physics_config.h`, and `TRAY_DEPTH_MM` is derived from them: 50.3125 mm.
`trayToScreenPx` (`src/render/tray_layout.{h,cpp}`) replaced `drawDice`'s inline mapping, `typeBand`
joined `resultsBand`, and `drawFrame` draws the strip every frame. `FrameCanvas::begin` fails with
"screen is not the 240x135 the tray is laid out for" on any other panel.

Each Definition of Done scenario, on 2026-11-22:

- **The layout.** `test_the_tray_fills_the_screen_under_the_strip_at_the_d20s_24_px_edge` failed
  first against `trayToScreenPx` on the 59 mm tray (the far wall at 0.14 px, not 20) and passed once
  the depth was derived. `test_before_the_dice_lock_the_strip_shows_the_type_alone` covers `typeBand`.
- **The type before the lock.** The operator launched rolls on the Cardputer: the strip showed the
  type alone while the dice flew, and the results once they locked.
- **Another screen.** Read in the code, as planned; no board with another screen exists.
- **Fairness (invariant #2).** `npm test`: 177 tests passed, the one-die fairness test in 336 s (the
  machine was also flashing and verifying a board). `npm run test:fairness`: 4 tests in 208 s.
- **On the devices.** Both envs built. `npm run verify:roll` passed on both boards, three dice a roll:

  | Board | D6 chi-square p | D20 chi-square p | Late frames |
  |---|---|---|---|
  | Cardputer ADV | 0.863 | 0.642 | 0 |
  | StickC Plus 2 | 0.731 | 0.958 | 0 |

  In all four captures each die's face up matched its chip, and no die lay under the strip: the
  Cardputer's last D20 roll left the red die against the top wall and the StickC's last D6 roll the
  white one flush with it, each wholly in view. The StickC's first `verify:roll` after flashing
  failed at boot on the IMU (ESP-IDF error 259); the canvas step, the only one this slice changed,
  had passed, and the next reset ran clean. The ledger's open item on that failure carries it.

The adversarial pass (Sonnet, correctness) found no defect. Its one risk, that ADR-0014's 0.1 s
level was tuned on the 59 mm tray, was measured with the level probe on the three upright takes,
20 rolls of three dice per take, against the shallower tray: the product's rule ended 39% of D6 and
30% of D20 within a quarter of the depth of the top wall (43% and 32% on the 59 mm tray), against
44% and 41% for the control that holds the level from before the shake. The rule still does as well
as the control. Its nit that a polygon edge on the far wall can round into the strip's last row was
rejected: the strip is drawn after the dice, so at most one row of a die's edge is covered, never
its face.

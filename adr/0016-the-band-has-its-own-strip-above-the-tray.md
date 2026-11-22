---
id: '0016'
title: "The band has its own 20 px strip above the tray, and the tray is the 115 px beneath it"
type: architecture
status: accepted
date: 2026-11-22
supersedes: []
superseded_by: []
---

## Context

Slice 0011 made the tray fill the whole 240 × 135 px screen: 240 px across 105 mm is ADR-0007's
24 px D20 edge (2.29 px/mm), and the 59 mm depth filled the 135 px height. The results band, a
20 px strip of the type, each die's result and the total, was drawn over the top 8.75 mm of that
tray once every die locked.

That is the moment the dice lie against the walls. Slice 0015's upright takes ended 43% of D6 and
32% of D20 within a quarter of the depth (14.75 mm) of the top wall, and a D20 is about 20 mm
across, so a die against that wall had close to half of itself under the band. Both boards'
captures from slice 0012 show it, and the ledger logged it.

Three ways out were weighed:

| Option | Cost | Why taken or not |
|---|---|---|
| **Shorten the tray to the 115 px under the band** | The tray is 50.3 mm deep instead of 59; a physics constant changes, so both fairness halves rerun (invariant #2) | Taken: nothing is ever covered, and the scale, so ADR-0007's 24 px edge, stays |
| Keep 59 mm and shrink the scale to fit 115 px | 1.95 px/mm, a D20 edge of about 20.5 px, and 35 px of bare screen at the sides | ADR-0007 chose 24 px for legible numerals; this gives it up and wastes the width |
| Keep the tray; move the band to the emptier edge, shrink it to the 1× font, or make it see-through | Each still covers a die some of the time | Dice rest against both walls, a 10 px band still covers, and a see-through band still hides the face |

Three D6, the bulkiest dice (16 mm edge, 27.7 mm space diagonal), still fit a 105 × 50.3 mm floor
with room to tumble.

## Decision

- **The top 20 px of the screen is the band's strip, and the tray is the 240 × 115 px beneath
  it**, at the same 2.29 px/mm. The tray's depth is derived from the screen, the strip and the
  scale, not set on its own: 115 / (240 / 105) = 50.3125 mm.
- **The strip always shows the die type**, and once every die of a roll has locked, each die's
  result and the total after it. A player sees what the next roll throws before throwing it.

## Consequences

- No die is ever drawn under the band: the camera is orthographic, the tray's walls keep every
  vertex inside its box, and the box's top wall is the strip's bottom edge.
- The physics runs on a shallower tray, so the dice behave differently and the fairness test's
  two halves ran before this merged (slice 0017). Numbers measured on the 59 mm tray, ADR-0014's
  wall shares among them, describe that tray, not this one.
- Both boards are 240 × 135 px. A board with another screen size would have to change the
  derivation, so the product stops at boot if the screen it finds is not the one the tray is laid
  out for.

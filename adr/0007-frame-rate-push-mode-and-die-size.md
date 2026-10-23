---
id: '0007'
title: "60 fps with the panel push overlapped with drawing by DMA, and D20s with a 24 px edge"
type: architecture
status: accepted
date: 2026-10-23
supersedes: []
superseded_by: []
---

## Context

`docs/roadmap.md` left the frame-rate target to a render probe: how long a frame of three
flat-shaded D20s with face numerals takes on each board, including the panel push that
ADR-0001 carried over from Dragon Radar at 13.0 ms. That push is the SPI bus itself
(240 × 135 pixels × 16 bits at 40 MHz is 13.0 ms), so faster drawing cannot shorten it. The
only way past it is to draw the next frame into a second canvas while DMA sends the last one,
which costs a second 64.8 KB buffer that the StickC's heap might not hold.

The probe (slice 0006) drew three D20s spinning about different axes, 600 frames per run, in
two push modes (blocking: draw, then push; overlapped: draw one canvas while DMA pushes the
other) at two sizes (an edge of 12 px, the roadmap's figure, and 24 px). Numerals were drawn
on every face within 60° of the camera, more than a product is likely to show, so the cost is
an upper bound. Measured on 2026-10-23, in µs, 95th percentile:

| | Cardputer ADV | StickC Plus 2 |
|---|---|---|
| Drawing (clear, geometry, raster, numerals), 12 px edge | 874 | 1039 |
| Drawing, 24 px edge | 1114 | 1306 |
| Push, blocking | 12,991 | 12,988 |
| **Frame, blocking**, 12 / 24 px | 13,848 / 14,082 | 13,999 / 14,273 |
| **Frame, overlapped**, 12 / 24 px | 12,967 / 12,967 | 12,970 / 12,970 |
| Largest free DMA block before the second canvas (64,800 B) | 217,076 B | 110,580 B |

- **The bus sets the frame rate, not the drawing.** Three dice take about 1 ms to draw; the
  push takes 13 ms whatever is drawn. Overlapped, the frame is the push alone, a ceiling of
  77 fps on both boards, and it barely varied (maximum 12,974 µs over 600 frames).
- **The second canvas fits on both boards.** On the StickC it leaves about 45 KB in the
  largest DMA block, before audio, physics and the IMU's own allocations, none of which the
  probe ran.
- **Size costs almost nothing, and legibility decides it.** 24 px costs 0.24 ms more than
  12 px on the Cardputer and 0.27 ms more on the StickC. At 12 px the numerals in the 6 × 8
  font pile into one another and cannot be read; at 24 px each reads on its face (the PNGs in
  `artifacts/render-probe/`).

Three targets were weighed. 60 fps blocking keeps one canvas, but a frame then leaves only
about 2.4 ms of a 16.7 ms period on the StickC for physics, audio and the IMU, none of them
measured yet, because the CPU waits out the bus. 30 fps blocking has headroom, but tumbling
dice move half as smoothly. 60 fps overlapped hands the bus wait back to the CPU.

## Decision

- **The product renders at 60 fps**, one frame per 16.7 ms.
- **The panel push overlaps the drawing of the next frame**: two full-screen 16-bit canvases,
  one drawn while DMA sends the other. Of each frame on the StickC, about 1.3 ms goes to
  drawing the dice and about 15 ms is left for everything else, against about 2.4 ms if the
  push blocked.
- **A D20 has a 24 px edge**, about 46 px across, and its numerals are the 6 × 8 built-in font.
- Which faces show a numeral is left to the roll slice: the probe's 60° bound measured the
  cost of the most it could show, not what reads best.

## Consequences

- The bus transaction stays open across a run of DMA pushes. LovyanGFX's `endWrite` waits for
  the bus to go idle, so closing it after each push silently makes every push blocking; the
  probe's `runOverlapped` shows the pattern.
- The StickC gives up 64.8 KB to the second canvas. If a later subsystem cannot fit beside
  it, that is a new ADR weighing the CPU budget against the heap, with measured figures for
  both.
- The roadmap's 12 px face (`docs/roadmap.md`, Not planned) becomes 24 px; textured numerals
  stay out of scope, since a 24 px face is still too small for legible texture.
- 60 fps is a target, not a ceiling: the bus allows 77. The probe ran unpaced, so how a
  frame is paced to 16.7 ms, and whether a push that is not synchronised to the panel's own refresh tears,
  are left to the roll slice that builds the product's frame loop.

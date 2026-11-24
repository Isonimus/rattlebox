---
id: '0030'
title: "The shape limits are one set sized for the largest die, and the code that scans them keeps scanning all of them"
type: architecture
status: accepted
date: 2026-11-23
supersedes: []
superseded_by: []
---

## Context

`src/geometry/die_shape.h` declares `DIE_MAX_VERTICES`, `DIE_MAX_FACES` and `FACE_MAX_CORNERS`.
They are not only a bound on what `makeXxx` may build. They size, across the firmware:

- `DieShape` itself, held by value in the die catalog's table, one row per die type;
- `ContactMemory::tray`, which is `[MAX_DICE][DIE_MAX_VERTICES][TRAY_PLANE_COUNT]` of
  `ContactImpulse` and is cleared with `memset` every physics step by `rememberImpulses`;
- `world.cpp`'s `MAX_TRAY_CONTACTS_PER_DIE`, and through it the file-scope `contacts` array;
- `HitFinder::_trayNormal`, which `src/sound/hit_finder.cpp` scans in full, to
  `DIE_MAX_VERTICES` rather than to the die's own `vertexCount`, deliberately, so that a roll
  which changes the die type or count leaves no stale entry behind;
- `die_view.cpp`'s `posedPx` stack array.

The D12 is the first die that does not fit them. A dodecahedron has 20 vertices against 12, and
pentagons against 4 corners (slice 0029). So a limit that was a ceiling nobody reached becomes a
cost every die pays, and the question is who pays it.

Measured, not estimated. Both boards built at 12 vertices and 4 corners, then at 20 and 5:

| | Cardputer | StickC Plus 2 |
|---|---|---|
| RAM before | 43,284 B (13.2% of 327,680) | 42,760 B (13.0%) |
| RAM after | 51,732 B (15.8%) | 51,208 B (15.6%) |
| RAM added | 8,448 B | 8,448 B |
| Flash added | 396 B | 384 B |

`sizeof(DieShape)` goes from 644 to 740 bytes and `sizeof(ContactMemory)` from 3,600 to 5,904.

And the CPU, timing the product's own `stepWorld` and `HitFinder::find` natively at `-O2`, one
frame being four physics steps with three dice, two runs of each configuration:

| Die | vertices | at 12/4 | at 20/5 |
|---|---|---|---|
| D6 | 8 | 63.5 / 65.3 µs | 64.5 / 65.6 µs |
| D8 | 6 | 45.5 / 46.3 µs | 45.8 / 46.3 µs |
| D10 | 12 | 59.1 / 59.3 µs | 60.8 / 59.5 µs |
| D20 | 12 | 53.6 / 54.0 µs | 54.5 / 54.2 µs |
| D12 | 20 | n/a | 78.2 / 76.5 µs |

The first pass of that table suggested a clean 1.1% to 1.6% penalty on the dice already shipped.
The second showed that figure sitting inside the host's own run-to-run spread, which reaches 2.8%
within a single configuration. **What the measurement establishes is a bound, under about 2%, not
a value.** The D12 itself costs 1.42× the D20 per frame, which the host is entitled to predict
because it puts the D10 at 1.10× the D20 and the device measured 4.4 ms against 4.0 ms, the same
ratio.

## Decision

**One set of limits, sized for the largest die the product builds, raised when a new die needs
more. `DIE_MAX_VERTICES` becomes 20 and `FACE_MAX_CORNERS` becomes 5; `DIE_MAX_FACES` stays 20.**

**`src/sound/hit_finder.cpp` goes on scanning the full `DIE_MAX_VERTICES`.** Narrowing it to each
die's own `vertexCount` was the obvious saving and was measured before being rejected: it is
worth under 2% on a path already at a quarter of the frame budget, and it would cost correctness
work, because that scan is exhaustive precisely so that a roll which changes the die type or
count cannot leave a stale normal behind and report a hit that did not happen. Buying an
unresolvable saving with a new reset hook and a new way to be wrong is a bad trade.

**A new die type's limits are raised in the same change as the die, and the raise is measured on
both boards before it merges**, because the raise is a change to every die's physics input and
therefore to invariant #2.

## Consequences

- The firmware carries 8,448 bytes of static RAM it does not use while a D6 is on screen. On
  327,680 bytes that is 2.6 percentage points, and the boards sit at 15.8% and 15.6% after it.
  This is the price of the decision and it is paid once, not per die.
- The D12 is the last die that moves these limits. A D4 is four vertices and four triangles, and
  d% is two D10s (`docs/roadmap.md`), so both fit what the D12 leaves behind.
- A later operator who notices `hit_finder.cpp` scanning empty vertices and "fixes" it will save
  nothing measurable and may reintroduce stale hits. The reason is recorded at that site as well
  as here (stele:ADR-0012).
- If a die ever needs enough vertices that the RAM stops being affordable, this decision is the
  one to supersede, and the replacement is per-shape storage rather than a narrower scan: the
  scan is not where the memory goes.
- The measurement method stands for the next raise: build both boards at each limit for RAM, and
  time `stepWorld` plus `HitFinder::find` natively for CPU, with at least two runs per
  configuration, because one run invited a conclusion the second withdrew.

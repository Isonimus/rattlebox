---
id: '0006'
title: "Render probe: what a frame of three flat-shaded D20s costs on each board, and whether drawing can overlap the panel push"
type: slice
status: accepted
date: 2026-10-23
supersedes: []
superseded_by: []
---

## Goal

Answer the M0 render question in `docs/roadmap.md` with measured numbers: how long a frame of
three flat-shaded D20s with face numerals takes on each board, including the panel push, and so
what frame rate the product can hold. ADR-0001 carried over Dragon Radar's 13.0 ms to push a
full canvas on both boards. That figure is the SPI bus itself (240 × 135 pixels × 16 bits at
40 MHz is 13.0 ms), so faster drawing cannot shorten it; the only way past it is to draw the
next frame while DMA pushes the last one. That needs a second 64 KB frame buffer, and whether
the StickC's heap holds one beside everything else is itself a measured question. The answer
goes into a new ADR, which sets the frame-rate target that the physics step and the roll slice
build on.

## Definition of Done

- **Given** a clean checkout
- **When** `cardputer`, `m5stickc-plus2`, the four IMU probe envs, `cardputer-render-probe` and `m5stickc-plus2-render-probe` build
- **Then** all eight build with no warnings from code under `src/`

- **Given** a D20 mesh built for a given edge length
- **When** the native tests inspect it
- **Then** it has 12 vertices and 20 faces, every edge has that length, every face winds outward (its normal points away from the centre), and opposite faces carry numerals summing to 21

- **Given** a die's orientation and a top-down orthographic camera
- **When** its faces are projected and shaded
- **Then** a face whose normal points away from the camera is culled, a front face's shade rises with how directly it faces the light and never falls below the ambient floor, and a numeral is placed only on faces within the numeral angle of the camera, at the face's projected centre

- **Given** either board running the render probe firmware
- **When** it boots
- **Then** it renders the scene for a fixed number of frames in each mode it can run (blocking push; drawing overlapped with a DMA push into a second buffer) at each die size, and prints, per run, the median, 95th percentile and maximum time of each phase (clear, geometry, raster, numerals, push or push wait, whole frame), then `[render] done`

- **Given** a board without heap for the second frame buffer
- **When** the probe reaches the overlapped mode
- **Then** it prints that the mode is unavailable with the size it needed and the largest free block, and runs the remaining runs, rather than failing boot or skipping silently

- **Given** a board running the probe
- **When** `node scripts/render-probe.mjs --env <env> --port <port>` runs
- **Then** it resets the board, fails on any boot failure, error line or panic, or a missing run, and otherwise writes `artifacts/render-probe/<env>/summary.{json,md}` with every run's timings and a PNG of the last frame

- **Given** the probe's summaries from both boards
- **When** the frame-rate target is chosen
- **Then** an ADR records the target, the push mode and the die size it assumes, with the measured numbers they rest on

## Design

**Workload.** Three D20s spread across the 240 × 135 tray so they do not overlap, each
spinning about its own axis at its own rate, so 600 frames per run cover many orientations
and the count of visible faces varies as it will in play. Each frame:

1. **clear**: fill the canvas with the tray colour (a 64 KB memory write);
2. **geometry**: rotate each die's vertices, cull back faces, shade the rest;
3. **raster**: fill each front face as a triangle into the canvas (`fillTriangle`);
4. **numerals**: draw the face's number at the projected centre of each face pointing at
   the camera (built-in 6 × 8 font), as `docs/roadmap.md` v0.1 specifies;
5. **push**: send the canvas to the panel.

Each phase is timed with `esp_timer_get_time()` and kept per frame in a buffer allocated at
boot, so no serial output lands inside a run. Statistics are computed on the board and
printed after each run: 600 frames of six phases is small, and the board already holds them.

**Die size.** `docs/roadmap.md` puts a D20 face at about 12 px across, so the probe runs at an
edge of 12 px and at 24 px. Fill cost grows with area, so the two sizes show whether a larger
die is affordable, which the roll slice may want for legibility.

**Two push modes.**

- **Blocking**: draw, then `pushSprite`, which returns when the bus is done. Frame time is
  the sum of the phases.
- **Overlapped**: two canvases. Draw into one while `pushImageDMA` sends the other, then
  `waitDMA()` and swap. Frame time is about the larger of drawing and the push. The second
  canvas is allocated when the mode starts and freed after it; if the allocation fails, the
  probe prints the size it needed and the largest free block, because "the StickC cannot
  hold two frames" is a finding the ADR needs, not a crash.

**Projection and shading.** Orthographic, looking straight down into the tray, as the roadmap's
top-down camera specifies. Dice in the probe never overlap and a convex die's front faces never
overlap each other, so back-face culling is the whole visibility test: no depth buffer, which
would cost a third 64 KB block, and no face sort. Flat shading is Lambert from one fixed light
plus an ambient floor, so no face goes black, packed to RGB565.

**Code layout.**

- `src/geometry/`: `Vec3`, a rotation as a 3 × 3 matrix, and the D20 mesh (vertices, faces,
  numerals). Pure, with no Arduino or LovyanGFX dependency, because the physics core will
  use the same mesh for its per-vertex contacts (ADR-0002) and must build natively.
- `src/render/`: the pure step from a posed die to screen-space faces (cull, shade, numeral
  placement), host-tested; and the device half that hands those faces to LovyanGFX.
- `src/probes/render/render_probe_main.cpp`: the probe entry point. Each probe moves into its
  own folder (`src/probes/imu/`, `src/probes/render/`), because today's IMU probe envs build
  everything under `src/probes/` and two entry points would both define `setup()`. A folder
  per probe lets each env take its own folder, where excluding the other probe's files by
  name would need editing every probe env whenever a probe is added.

**Existing code considered.** The canvas set-up is `FrameCanvas` in `src/main.cpp`: a 16-bit
full-screen `LGFX_Sprite`, allocated once and checked. The probe needs two such canvases, one
allocated mid-run, so it does not reuse the class as it stands. Moving it into a shared header
for one other caller would be the speculative step the rule of three warns against; the roll
slice, the third user, decides its shape. The boot steps, `logStarted` and `haltOnFailure`
follow the IMU probe's entry point. Those are about 20 lines, now in their second copy, and are
accepted under the same rule. The screen capture (`src/debug/screen_capture`) is reused as it
is, so the PNG shows the frame that was timed. Searched for existing 3D or vector code: none in
this repo, and Dragon Radar draws only 2D.

**Host side.** `scripts/render-probe.mjs` starts `scripts/serial_reset_monitor.py` to reset the
board and read its log, as `scripts/skeleton-verify.mjs` does, parses the `[render]` lines, then
takes the PNG with `scripts/capture_screen.py`. It is a probe, so it is exempt from
`package.json` wiring (R11). Its parsing and summary are pure functions with tests.

**Envs.** `cardputer-render-probe` and `m5stickc-plus2-render-probe` extend the product envs
and swap `main.cpp` for the probe entry, as the IMU probe envs do. Invariant #1 applies: shared
code is added, so every env builds.

## Verification

- **Native (`pio test -e native`)**: `test_geometry` for the D20 mesh (counts, edge lengths,
  outward winding, opposite numerals summing to 21) and rotation; `test_render` for culling,
  shading bounds and monotonicity, the RGB565 packing, and numeral placement.
- **Node (`scripts/render-probe.test.mjs`)**: parsing of the `[render]` result and unavailable
  lines, failure on an error line or a missing run, and the summary table.
- **Device**: `node scripts/render-probe.mjs` on both boards; `artifacts/render-probe/<env>/`
  is the evidence the ADR cites. As a probe it is exempt from `package.json` wiring (R11).
- **Builds**: all eight envs from clean, counting warnings from `src/`.

## As built

Shipped on 2026-10-23. The probe's answer is ADR-0007: 60 fps, the panel push overlapped with
drawing by DMA, and D20s with a 24 px edge. Drawing three dice took 1.1 to 1.3 ms at the 95th
percentile and the push 13.0 ms, so the bus sets the frame rate; overlapped, a frame was the
push alone (12,970 µs on the StickC), and the second canvas fitted on both boards.

| Definition of Done | Evidence |
|---|---|
| Eight envs build with no warnings from `src/` | All eight from a clean build, 0 warnings from `src/` (LovyanGFX's own headers warn under `-Wextra`, as before) |
| The D20 mesh is a closed, outward-wound icosahedron with opposite faces summing to 21 | `test_geometry`: every edge at the requested length, five faces per vertex and two per edge, every face's winding against its centroid, numerals 1 to 20 once each with opposites summing to 21 |
| Projection culls, shades and places numerals | `test_render`: front faces counted against an independent count of normals toward the camera, a face turned to the camera drawn and its opposite not, the numeral angle, each numeral over its own face's centre with screen y down, full colour square to the light, the ambient floor, shading order, RGB565 packing |
| The board prints every run's statistics, then `[render] done` | Both boards: four runs each, in `artifacts/render-probe/<env>/summary.{json,md}`; the statistics are `test_frame_stats` (nearest rank, so every figure is a real frame) |
| A board without heap for the second canvas says so and goes on | Not reached on either board (largest free DMA block 217,076 and 110,580 bytes against 64,800 needed); the host's handling of the line is tested in `scripts/render-probe.test.mjs` |
| `render-probe.mjs` fails on a broken log and writes the summary and PNG | `scripts/render-probe.test.mjs`: a missing run, a missing `done`, a failed boot, an error line, and a blocking mode excusing itself; both boards passed and wrote their artifacts |
| An ADR records the target, push mode and die size with their numbers | ADR-0007 |

**Deviations from the design.**

- **`Vec3` moved** from `src/imu/imu_sample.h` to `src/geometry/vec3.h`, with the operations
  the mesh and projection need. The IMU had the only copy; a second, for geometry, would have
  been the duplicate the design's reuse check exists to catch.
- **The final frame changed** from the 12 px die at frame 0 to the 24 px die mid-run. The
  first Cardputer capture showed the 12 px numerals piled into one another, and all three dice
  in the same orientation; the capture of the larger die is the evidence the size decision
  needed. The 12 px frame is kept beside it as `edge-12-frame-0.png`.
- **`parseLine` from `scripts/skeleton-verify.mjs` is reused** for boot and error lines rather
  than its regexes copied, so both device scripts fail on the same lines.
- **The design listed no test for the statistics**; `test_frame_stats` was added, since the
  ADR's figures are its output.

**Tests that did not bite.** Four faults were planted to check the new tests. Reversing the
opposite faces' winding, dropping the ambient floor and lighting faces from behind each failed
a test, but drawing screen y upward passed: the first version of that test looked for a vertex's
pixel, and the die's outline is symmetric, so a mirrored picture put another vertex on the same
pixel. The test now checks each numeral over its own face's centre, which the mirror moves,
and fails on that fault.

**Adversarial review.** One correctness reviewer (Sonnet, blind to intent) found no defect
and five weak points. Accepted: the final frame went out by `pushSprite` while the capture reads
the canvas, so nothing showed the DMA path sends correct pixels, and the final frame now goes
out by the overlapped mode's DMA push (the StickC's panel was compared with its PNG); the host
script had no handler for the monitor failing to start; and the culling test counted front
faces the way the renderer does, so a hand count for the rest pose (8 up, 4 edge-on, 8 down)
was added, which fails when edge-on faces are drawn. Rejected: a panic after `[render] done`
surfaces only as a failed capture, which still fails the run loudly with the capture's reason;
and `finish()` running twice settles an already settled promise, which changes nothing.
The StickC was re-run for the DMA final frame, and ADR-0007 cites that run.

**Found while building.** LovyanGFX's `endWrite` waits for the bus to go idle, so the
overlapped mode keeps one transaction open across the whole run; closing it per push would
have measured a blocking push under the overlapped name. Measured, the overlapped frame came
out at the push alone, which a blocking push could not.

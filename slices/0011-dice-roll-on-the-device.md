---
id: '0011'
title: "The dice roll on the device: the product's frame loop, the physics at 60 fps, a launch from the controls or the serial port, and the results overlay"
type: slice
status: accepted
date: 2026-10-24
supersedes: []
superseded_by: []
---

## Goal

Put the dice in the product. The firmware still runs slice 0003's skeleton, a readout of raw IMU
samples, while the physics, the renderer and the shake input have each been built and
measured on their own. This slice builds the first half of v0.1 (`docs/roadmap.md`): dice that
roll on the device's screen, launched from rest by the controls or by the serial port. Shake to
roll, tray gravity, level capture and the spin at detection are slice 0012's: they need a person
shaking the board, while everything here can be verified with nobody holding it.

It closes three ledger items:

- **The product's frame loop is not built** (ADR-0007). This slice builds it: two canvases, the
  DMA push overlapping the next frame's drawing, paced to 60 fps. It checks whether an
  unsynchronised push tears. Neither board wires the panel's tearing-effect pin
  (`pin_busy = -1` in both display configs), so no push can be synchronised to the panel's
  refresh.
- **The physics is not yet in the product** (ADR-0009). This slice:
  - runs four 1/240 s steps per frame;
  - launches from `esp_random()` with an entropy source on;
  - nudges cocked dice;
  - measures a step's cost on the StickC;
  - checks whether a speculative contact's early bounce shows;
  - shows each locked result, since a die can be knocked onto another face after it locks.
- **GCC 14.2 has never run the physics** (ADR-0009). A device capture must show locked results
  matching the face up.

## Definition of Done

- **Given** the fairness test and probe as committed in cb0a5e7
- **When** the per-step roll logic (step, advance results, nudge, lock) moves out of
  `src/probes/fairness/` into product code that the device, the test and the probe all call
- **Then** `npm test` and `npm run test:fairness` pass, and `scripts/fairness-probe.mjs` prints
  the same results, number for number, as it did before the move

- **Given** one D20, then one D6, launched from rest on a level tray, as a type or count change launches them
- **When** the host fairness test rolls 18,000 D20 and 3,400 D6 results this way, seed 1
- **Then** each passes chi-square at α = 0.001

- **Given** the product's die choice, at boot one D20
- **When** PRIMARY is pressed, SECONDARY is pressed, or (Cardputer) UP or DOWN is pressed
- **Then** PRIMARY toggles D20 and D6 and SECONDARY cycles the count 1, 2, 3, 1. UP and DOWN add
  or take one die, stopping at 3 and 1, and a press that changes nothing launches nothing. Every
  change launches the new dice from rest (ADR-0009).

- **Given** either board running the product
- **When** it boots
- **Then** it brings up power, the display, both canvases, input, the IMU and the entropy source,
  or halts with the failing step named, as slice 0003's boot does. It then launches one D20 and
  prints `[boot] <board> ready`.

- **Given** a roll in progress on either board
- **When** frames are drawn
- **Then** each frame is paced to 1/60 s and runs four physics steps, and the board prints one
  `[frame]` line per roll. The line gives the frame count, the mean and longest frame, the
  longest four-step physics time, and how many frames ran past their deadline.

- **Given** a roll whose dice have all locked
- **When** the next frame is drawn
- **Then** a band shows the die type and each result in its die's colour, and the total for more
  than one die. The board prints `[roll] locked` with the type, the results, the faces showing,
  the seconds since the launch and the number of nudges. The band stays until the next launch.

- **Given** the product on the serial port
- **When** the host sends `R`, `T` or `C`
- **Then** `R` launches the current choice again, and `T` and `C` act as PRIMARY and SECONDARY do.
  `S` still answers a screen capture, and any other byte is counted and reported once, as before.

- **Given** either board running the product on the port
- **When** `npm run verify:roll -- --env <env> --port <port>` runs
- **Then** it resets the board, fails on any boot failure, error line or panic, and rolls three D6
  40 times and three D20 60 times through `R`. It fails if a roll does not lock within 20 s or if
  either die type's results fail chi-square at α = 0.001. It writes
  `artifacts/roll-verify/<env>/`: summary.json, summary.md with the frame and physics timings,
  and a capture after the last roll of each type.

- **Given** those captures
- **When** a person compares each die's face up with the band
- **Then** they match, which closes the GCC 14.2 audit (ADR-0009)

- **Given** rolls on each board, watched by a person
- **When** dice fly fast into walls and the tray is redrawn at 60 fps
- **Then** As built records whether the push tears and whether a speculative contact's early
  bounce shows. A visible defect becomes a ledger item, or a fix in this slice.

- **Given** a clean checkout
- **When** `cardputer`, `m5stickc-plus2` and both skeleton-check envs build
- **Then** all build with no warnings from code under `src/`, and `npm run verify:skeleton`
  runs against the skeleton-check firmware, which keeps slice 0003's readout

## Design

**One roll core, shared.** The device, the fairness test and the fairness probe all advance a roll
the same way, one step at a time:
- step the world;
- update each die's result;
- nudge a die that came to rest cocked, and start its result over;
- report when every die has locked.

Today that logic is `advanceResults` and `everyDieLocked` in `src/probes/fairness/simulated_roll.cpp`.
`simulateRollIn` and `simulateShakenRoll` call them, and the device would be the third copy. They
move to `src/roll/dice_roll.{h,cpp}`. A `DiceRoll` there holds the world, the results, the nudge
count and the steps taken, with `launchRoll` and `stepRoll`. The probe's two simulations call the
same functions. Invariant #2 then tests the code the device runs, not a sibling. The move keeps
every RNG draw in its order, so the probe's output must not change by one count, which makes the
move its own regression check.

**Launch rolls are tested again.** A change of type or count throws new dice from rest
(`launchDice`, ADR-0009), and so does `R`. Since ADR-0010, `test/test_fairness` rolls only chained
shakes. A launch from rest appears there only as each run's first, unchecked roll, so the
product's launch path has no test. ADR-0009's launch test comes back for one die of each type, at
ADR-0009's sizes, chi-square only. A launch draws a uniformly random orientation, so it carries
nothing over from the previous result, and the repeat check has nothing to measure. Its cost is
measured and reported. Invariant #2's row is unchanged: the new test adds to it.

**The frame loop** (`src/main.cpp`), one pass per frame:
1. Read the input and the serial bytes. The IMU is brought up at boot but read from slice 0012
   on, when it first drives something.
2. Run four `stepRoll` calls, timed together.
3. Draw the tray, the dice and, once locked, the band into the back canvas.
4. Wait for the previous DMA push, start this one, and swap canvases. The bus transaction stays
   open across pushes (ADR-0007).
5. Wait until the frame's deadline.

The pacing is a pure function in `src/app/frame_pacer.{h,cpp}`, tested on the host:
- the deadline advances by 16,667 µs;
- a frame that ends past it starts the next at once;
- a stall longer than a whole frame (a screen capture takes seconds on the StickC) restarts the
  cadence from now instead of rushing to catch up.

The physics is tied to frames, not to wall time: four steps each frame, whatever the frame took.
A slow frame slows the dice rather than skipping steps, so the results come from the same
1/240 s steps the fairness test runs (ADR-0009).

**Timing, without a buffer per frame.** ADR-0007 left the StickC 45 KB of DMA heap beside two
canvases. Per-frame durations for a 20 s roll would take 9.6 KB, so instead the loop keeps four
running figures per roll: the frame count, the sum, the longest frame and the longest physics.
It also counts frames past their deadline. `src/probes/render/frame_stats` is not reused: it sorts
a full buffer to find percentiles, which is what this avoids.

**What is drawn.**
- The tray maps millimetres to pixels at 240 / 105 = 2.29 px/mm. That is ADR-0007's 24 px D20
  edge, and the 59 mm depth fills the 135 px height. The screen's y runs down where the tray's
  runs up.
- Dice are drawn lowest first, by the centre's height, so a die on top of another covers it.
- Each die has its own colour, the render probe's three, and its result in the band uses that
  colour. That is how the player tells which result is which die.
- Which faces show numerals is ADR-0007's open question. The slice starts at the probe's 60° and
  settles it by eye from the captures. As built records the angle chosen.
- The band runs along the top edge and appears only once every die has locked. It covers a strip
  of the tray, and any die under it shows its result in the band itself. The text and the layout
  are a pure function in `src/render/results_band.{h,cpp}`, tested on the host. Its drawing is a
  thin LovyanGFX layer, as `die_drawing` is for `die_view`.

**The die choice** is a pure function, `src/roll/dice_choice.{h,cpp}`: a choice and an
`InputAction` in, the new choice and whether it changed out. Tested on the host. One D20 at boot,
since NVS presets are v0.2's.

**Randomness on the device.** `src/app/hardware_source.h` is a `UniformSource` that takes the top
24 bits of `esp_random()`. `esp_random()` is a true RNG only with the radio on or after
`bootloader_random_enable()` (ADR-0002). Neither radio is used, so boot enables the latter and
leaves it on. While it is on, the SAR ADC is taken. Nothing in the firmware reads the ADC (no
`analogRead` under `src/`). The first use of the ADC, a battery gauge for example, must deal with
this.

**Serial commands** are single bytes, like the screen capture's `S`, so the host's command
protocol stays one protocol. `serviceScreenCapture` becomes the dispatcher. The mapping from a
byte to a command is a pure function, tested on the host. The `[roll]` and `[frame]` lines are
the verify script's input.

**The skeleton's readout keeps its check.** `scripts/skeleton-verify.mjs` checks the HAL against
the readout's `[imu]` and `[input]` lines. The product replaces that readout, so the readout moves
to `src/probes/skeleton/` with an env per board, built as the probes are. The verify script then
runs against it. The folder holds every alternative entry point, so the product envs already
leave it out.

**Existing code considered.**
- **Reused:** `launchDice`, `nudgeCockedDie`, `updateResult` and `stepWorld` unchanged. Also
  `viewDie` and `drawDieFaces` / `drawDieNumerals` from the render probe; the boot steps;
  `serviceScreenCapture`; the render probe's overlapped push (`startDmaPush`, `waitDMA` inside one
  open transaction); and the skeleton verify script's `parseLine` for boot and error lines, as the
  render probe's script already does.
- **Moved to be shared:** `advanceResults` and `everyDieLocked`.
- **Not reused:** `frame_stats` (above), and `SeededSource`, which is the host's: the device's
  randomness is the hardware's.

## Verification

- `test/test_dice_roll`: a launch starts every result over; a step advances the world; a die
  resting cocked is nudged and starts over; the roll reports settled only once every die has
  locked.
- `test/test_dice_choice`: every action from every choice, including the presses that change
  nothing.
- `test/test_frame_pacer`: on time, late by less than a frame, and stalled for more than one.
- `test/test_results_band`: the text for one, two and three dice of each type, and the total.
- `test/test_fairness`: the two launch-from-rest tests, beside the chained tests.
- `scripts/fairness-probe.mjs`, run before and after the move, compared line by line.
- `scripts/roll-verify.mjs`, wired as `npm run verify:roll`, with its parsing and statistics
  tested in `scripts/roll-verify.test.mjs`, which runs in `npm test`. Its device half needs a
  board on the port; no CI runs it (LEDGER).
- The captures and the watched rolls, compared by a person, for the band matching the faces, for
  tearing and for early bounces.
- `pio run` for `cardputer`, `m5stickc-plus2`, `cardputer-skeleton` and `m5stickc-plus2-skeleton`.

## As built

Merged 2026-10-24. Every Definition of Done scenario was met. The deviation and the measurements:

- **The roll core moved without changing a number.** The fairness probe ran before and after the
  move to `src/roll/dice_roll`. Its `summary.json` was identical apart from each run's wall time.
  `simulateRollIn` calls `stepWorld` and `advanceResults` itself rather than `stepRoll`, because
  its observer watches between the two, where the probe's speed trace always ran. `stepRoll` is
  the device's form of the same two calls.
- **The launch-from-rest tests pass and are cheap.** One die of each type added about 7 s to
  `npm test`; its fairness test took 157 s, up from 150 s. Three dice of each type went into
  `npm run test:fairness`, which took 92 s, up from 88 s.
- **On the device, with `npm run verify:roll`:** three dice a roll.

  | Board | Die | Rolls | p | Mean / longest roll (s) | Longest frame work (µs) | Longest four steps (µs) | Late frames |
  |---|---|---|---|---|---|---|---|
  | StickC | D6 | 40 | 0.983 | 0.50 / 0.72 | 11,176 | 4,560 | 0 |
  | StickC | D20 | 60 | 0.744 | 0.64 / 1.48 | 12,169 | 3,794 | 0 |
  | Cardputer | D6 | 40 | 0.685 | 0.47 / 0.75 | 11,347 | 4,213 | 0 |
  | Cardputer | D20 | 60 | 0.844 | 0.64 / 1.07 | 11,430 | 3,811 | 0 |

  - Four physics steps of three dice take at most 4.6 ms of the 16.7 ms frame.
  - A frame's work peaks around 12 ms, most of it waiting for the previous 13 ms DMA push, so no
    frame ran late.
  - One D20 result in the StickC's 180 was knocked onto another face after it locked, about as
    often as ADR-0009's roughly 1 in 500 per die predicts. The band showed the locked result.
- **GCC 14.2 reads results correctly.** In all four captures each die's face up matched its chip
  in the band:
  - StickC: D20 14, 13, 6; D6 4, 6, 5.
  - Cardputer: D20 13, 12, 13; D6 4, 4, 2.

  Every roll's `shown` matched its `results` apart from the one knocked-over die.
- **Watched by the operator on the StickC:** no tearing from the unsynchronised push, and no
  visible early bounce from a speculative contact.
- **Numerals stay at the probe's 60°.** A resting D20 shows its top numeral and its three
  neighbours' (41.8° off); the operator chose to keep that.
- **The skeleton's readout** runs as `cardputer-skeleton` and `m5stickc-plus2-skeleton`.
  `npm run verify:skeleton` now takes those envs, and it passed on both boards after the move. The
  StickC's first run failed its rest check: z rotation reached 11.9 °/s, with a spread of 1.28 °/s
  against 0.07 °/s on the rerun. The board had been touched; the rerun, left alone, passed.
- **Review fixes to the verify script.** The script waited for the `[frame]` line only after the
  `[roll] locked` line had arrived, so a frame line in the same chunk could be missed and the run
  failed after a 30 s timeout. It now waits for both before it sends a command, and a test
  reproduces the miss. A monitor that dies, or that fails to write to the board, now fails the run
  at once instead of hanging it. The StickC passed `verify:roll` again with the fixed script.
- **Deviation:** the design planned to read the IMU every frame without using it. That was dropped
  before building, as code that would do nothing until slice 0012. The IMU is brought up at boot
  only.

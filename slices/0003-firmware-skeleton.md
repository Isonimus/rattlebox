---
id: '0003'
title: "Firmware skeleton: Dragon Radar HAL copied in, both boards booting, raw IMU samples on one device frame"
type: slice
status: accepted
date: 2026-10-22
supersedes: []
superseded_by: []
---

## Goal

Get both boards running Rattlebox firmware that starts every subsystem it uses, stops loudly
when one fails, and streams raw IMU samples on the device frame ADR-0001 defines. This is the
first slice in `docs/roadmap.md` and the foundation the M0 probes run on. It carries the
project's largest risk: the Cardputer ADV's BMI270 has never been driven by our code (Dragon
Radar left it as a stub), and the product's only real input is the IMU. Getting the BMI270
working is the first job, ahead of any rendering or physics.

It creates the checks a CI workflow will run, but not the workflow itself, which is its own
slice; until then the CI ledger item stays open and invariant #1 holds by review.

## Definition of Done

- **Given** a clean checkout
- **When** `pio run -e cardputer` and `pio run -e m5stickc-plus2` run
- **Then** both build with no warnings from code under `src/`

- **Given** a clean checkout
- **When** `npm test` runs
- **Then** the native tests, the doc tooling tests and the host script tests all pass

- **Given** either board, powered on
- **When** it boots
- **Then** the serial log has one `[boot] <subsystem> OK` line for each of display, input, IMU and power, with the IMU line naming the chip and its configured ranges (at least ±8 g and ±2000 °/s), and the screen shows the live IMU readout

- **Given** a subsystem whose `begin()` reports failure
- **When** boot reaches it
- **Then** boot stops there, logs `[boot] <subsystem> FAILED` with the reason, shows the same on screen if the display came up, and does not start the later subsystems or the main loop

- **Given** an IMU read that fails on the bus (short read or NACK)
- **When** the driver reads a sample
- **Then** it reports the failure rather than returning the previous or a zero sample, and the log counts failed reads

- **Given** either board lying flat, screen up, and still
- **When** samples stream
- **Then** the acceleration magnitude is within 0.15 g of 1 g, `az` is the axis nearest +1 g, and every gyro axis is within 10 °/s of zero

- **Given** either board held with its top edge up, then with its right edge up
- **When** samples stream
- **Then** `ay`, then `ax`, is the axis nearest +1 g

- **Given** either board lying flat, screen up
- **When** it is twisted counterclockwise as seen from the screen
- **Then** `gz` peaks above +90 °/s

- **Given** either board
- **When** it is shaken hard
- **Then** the acceleration magnitude peaks above 2 g, proving the range is not the ±2 g that shaking saturates

- **Given** the Cardputer ADV or the StickC
- **When** the primary and the secondary input are pressed (Enter and Backspace, or buttons A and B)
- **Then** the log shows one `[input] PRIMARY` and one `[input] SECONDARY` line, one per press

- **Given** a raw sample from either chip
- **When** it is decoded and remapped
- **Then** the result is in g and °/s on the device frame (x right, y up the screen, z out of the screen), with the byte order and per-range scale of that chip

- **Given** either board running
- **When** the host sends `S` over the serial console
- **Then** it answers with the screen capture frame the Dragon Radar protocol defines, and the host writes a PNG of the screen

## Design

**What is copied from Dragon Radar** (`../esp32-dragon-ball-radar`, read on 2026-10-22), per
ADR-0001. Only what the skeleton uses comes across; audio, storage and GPS stay behind until a
slice needs them, so no code is parked here unused.

| Copied | Changes on the way in |
|---|---|
| `platformio.ini` `[common]`, `cardputer`, `m5stickc-plus2` and `native` envs | Platform and LovyanGFX pins kept exactly; TinyGPSPlus, the custom env and the GPS/compass filters removed |
| `src/hal/` interfaces: display, input, IMU, power; `hal.h`; `press_gesture.*` | Every `begin()` returns `bool`; `ImuHAL` rewritten (below); GPS, audio and storage dropped from `HardwareHAL` |
| `boards/cardputer/`: display, `cardputer_i2c`, input, `tca8418_keymap`, power, `cardputer_hal` | IMU stub replaced by the BMI270 driver |
| `boards/m5stickc/`: display, input, power, `m5stickc_hal` | MPU6886 driver moved into the board folder and rewritten for raw samples |
| `src/debug/screen_capture.*`, `scripts/capture_screen.py` and its tests | Header tag renamed from `DRSCREEN`/`DREND` to `RBSCREEN`/`RBEND`; the DTR/RTS fix from Dragon Radar's amendment kept |
| `test/test_press_gesture`, `test/test_tca8418_keymap`, the Arduino mocks they need | None |

Not copied: `main.cpp` (a game state machine with nothing to keep), the render layer, the
title logo. Dragon Radar's ledger has two items about code coming across: the StickC's
GPIO 37/39 buttons on `INPUT` with external pull-ups, and whether the generic `esp32dev`
variant matches the Plus 2. Both stay unconfirmed until this slice's device run, and if
still unconfirmed at merge, they enter this repo's ledger.

**Boot is fail-loud.** In Dragon Radar only `InputHAL::begin()` reports failure; the others
return `void` and the log says "OK" unconditionally. Here every `begin()` returns `bool` and
sets a reason, and `runBootSteps` (pure, host-tested) runs the named steps in order and stops
at the first failure. The display comes up first so later failures can be shown on screen.
The canvas is allocated once at boot and the allocation is checked; nothing is `new`ed in the
loop.

**`ImuHAL` delivers raw samples.** `begin()` configures the chip; `read(ImuSample&)` returns
`false` on any bus error. `ImuSample` holds acceleration in g and angular rate in °/s on the
device frame of ADR-0001, plus a timestamp in microseconds. The skeleton configures ±8 g and
±2000 °/s, since a shaken handheld spins fast as well as hitting hard. The M0 IMU probe
measures whether those, and the output rate, are right, and records the answer in an ADR.

**Decoding and axis remapping are pure and host-tested** in `src/imu/`. Each chip has a
decode function (raw register bytes to g and °/s: the BMI270 is little-endian, the MPU6886
big-endian, each scaled by its configured range). Each board declares one constant
`AxisMap` (for each device axis: which sensor axis, and its sign) that one generic function
applies. The host tests prove the decoding and that the remap applies a map correctly. Whether
each board's map constant is *right* depends on how the chip is mounted on the board, so only
the orientation checks on the device can prove it.

**BMI270 driver** (`boards/cardputer/bmi270.*`) on the shared internal I2C bus that
`cardputer_i2c` already owns (G8/G9, alongside the keyboard at 0x34). The bus address, the
init sequence and the config blob come from Bosch's BMI270 Sensor API (BSD-3-Clause) or M5Stack's
port of it, read on the day of the port; the source file, version and license go in the
blob's header. The blob is about 8 KB and Arduino-ESP32's `Wire` buffer is 128 bytes, so it is
written in chunks, advancing the chip's init address between them. After loading, the driver
reads the chip's internal status and fails `begin()` unless initialization reports success:
a BMI270 with a bad config load reads as all zeros, not as an error.

**MPU6886 driver** (`boards/m5stickc/mpu6886.*`). Dragon Radar's driver printed "not found"
and carried on, and its update silently skipped short reads, returning a stale heading. This
one fails `begin()` on a wrong `WHO_AM_I`, sets the ranges above, burst-reads accel and gyro
in one transaction, and returns `false` on a short read.

**The skeleton's screen** shows the board name, the six live values and a dot that moves
with the gravity direction in the screen plane. The dot makes a wrong axis map obvious to
the eye before the scripted check runs. Samples are logged to serial at 50 Hz as
`[imu] t ax ay az gx gy gz`, which over the StickC's 115200-baud bridge is about a quarter of
the link.

**Device check: `scripts/skeleton-verify.mjs`.** It spawns `pio device monitor -e <env>`, so
PlatformIO's pyserial handles the port and no native Node module is needed, and parses the
stream. Opening the port resets the board, which is wanted here: the check needs to see the
boot log. The check then prompts the operator through the orientations, the twist, the shake
and the two presses, and checks each against the Definition of Done within a timeout. It
fails on any `FAILED`, any failed IMU read, or any line from the ESP-IDF error log. It writes
`artifacts/skeleton/<env>/` with the raw samples as CSV, the measured rest noise (standard
deviation per axis) and the sample rate actually achieved, which the IMU probe starts from,
plus a screen capture PNG for review.

Its parsing and threshold logic is pure and lives apart from the process handling, so
`scripts/skeleton-verify.test.mjs` tests it against recorded log fixtures in CI. The device
half needs a board on the port, and no hardware-in-the-loop runner exists, so it runs by hand
and its results go in `## As built`. That gap is recorded in `LEDGER.md` rather than hidden
behind a CI step that pretends to run it.

## Verification

- **Builds**: `pio run -e cardputer` and `pio run -e m5stickc-plus2`, both clean (invariant #1).
- **Native tests** (`pio test -e native`): `test_press_gesture` and `test_tca8418_keymap`
  (copied); `test_boot_steps` (stops at the first failing step, names it, never runs a later
  step); `test_imu_decode` (each chip's byte order and range scale, including full-scale
  negative values; a decoder with swapped bytes or the wrong scale fails it);
  `test_axis_map` (each axis selection and sign is applied; a map that drops a sign fails).
- **Host script tests**: `scripts/skeleton-verify.test.mjs` (`node --test`) on recorded
  fixtures covering a passing run, a `FAILED` boot line, a failed IMU read, a rest
  magnitude out of tolerance, the wrong axis nearest +1 g, a negative twist peak and a
  shake that stays under 2 g; `scripts/test_capture_screen.py` (copied).
- **On device**: `npm run verify:skeleton -- --env cardputer` and `-- --env m5stickc-plus2`,
  each passing, with the artifact directories kept for review. Wired into `package.json`
  (R11); its device half runs by hand (see `LEDGER.md`).
- `npm test` aggregates the native, Node and Python suites.

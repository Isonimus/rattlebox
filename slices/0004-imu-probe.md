---
id: '0004'
title: "IMU probe: the accelerometer range, output rate and filtering that tell a shake from handling"
type: slice
status: accepted
date: 2026-10-23
supersedes: []
superseded_by: []
---

## Goal

Answer the M0 IMU question in `docs/roadmap.md` with measured numbers: which accelerometer
range, output rate and filtering let the firmware tell a deliberate shake from the device
being handled (picked up, carried, set down, tilted, bumped). ADR-0001 fixed only a floor of
±8 g and deferred the rest to this probe, and slice 0003 showed the floor is not enough to
measure with: a hard StickC shake read 8.0 g, the top of the range, so how hard a shake really
is was never seen. A roll starts on a shake and captures its "level" reference then
(`docs/roadmap.md`, v0.1), so a detector that fires on handling would start rolls nobody asked
for, and one that misses gentle shakes would ignore the player.

The answer is recorded in a new ADR with the numbers, and both boards' product firmware is
switched to the chosen settings in this slice, so the next slice builds on measured settings
rather than the skeleton's placeholders.

## Definition of Done

- **Given** a clean checkout
- **When** `cardputer`, `m5stickc-plus2`, `cardputer-imu-probe` and `m5stickc-plus2-imu-probe` build
- **Then** all four build with no warnings from code under `src/`

- **Given** an IMU settings request (accelerometer range, gyro range, output rate)
- **When** a chip's driver translates it to register values
- **Then** a combination the chip supports yields that chip's documented register values, and one it does not support is refused with a reason naming the field, never rounded to the nearest supported value

- **Given** a driver whose settings were never set, or were refused
- **When** boot reaches the IMU
- **Then** boot stops with `[boot] imu FAILED` and the reason

- **Given** either board running the probe firmware
- **When** the host sends `R`
- **Then** the board records one window of samples at the chip's probe rate with no serial output while recording, then replies `RBIMU <rate_hz> <accel_range_g> <gyro_range_dps> <count> <bytes>`, the samples, and `RBEND`

- **Given** a recorded window
- **When** the host reads it
- **Then** a header that does not match its byte count, a short body, a missing `RBEND`, or a gap between consecutive timestamps over 1.5 sample periods fails the capture with the reason, rather than analysing partial or uneven data

- **Given** a device-frame value from either chip
- **When** it is packed for the wire and unpacked on the host
- **Then** it comes back exactly, except that +full scale (only reachable by negating a −32768 count) comes back one count low, and a packed value at either end of the range is reported as saturated

- **Given** the operator performs every gesture in the protocol three times on each board
- **When** `node scripts/imu-probe.mjs --env <env> --port <port>` analyses the captures
- **Then** it writes each capture as CSV and a summary of, per gesture: peak |a|, the count of saturated samples, peak |ω|, and, per candidate rate and detector feature, the worst shake value, the worst handling value and the margin between them

- **Given** the probe's summaries from both boards
- **When** the settings are chosen
- **Then** an ADR records the range, output rate, filter and detector threshold with the measured numbers they rest on, and both boards' product firmware runs those settings

- **Given** both boards running the product firmware with the chosen settings
- **When** `npm run verify:skeleton` runs on each
- **Then** every check passes, with the range floor in the check raised to match the ADR

## Design

**Two firmwares, one HAL.** The probe is its own entry point, `src/probes/imu_probe_main.cpp`,
built by two new envs, `cardputer-imu-probe` and `m5stickc-plus2-imu-probe`, which `extends`
the product envs and swap `main.cpp` for the probe in `build_src_filter`; the product envs gain
`-<probes/>`. Everything below the entry point is the same code, so what the probe measures is
what the product will run. Invariant #1 applies: the IMU HAL changes, so all four envs build.
The probe stays in the repo after this slice: the ADR's numbers can only be re-measured with it,
and the dice cup on the roadmap (a face-down shake) is a second question for the same tool.

**IMU settings become data.** Today each driver fixes its range and rate in constants
(`FULL_SCALE`, `CONF_100HZ_NORMAL_PERF`, `SETTINGS[]`). The probe needs ±16 g at the chip's
fastest useful rate, and the product needs whatever the ADR picks, so the settings move into an
`ImuSettings` struct (`src/imu/`): accelerometer range in g, gyro range in °/s, output rate in Hz.
`ImuHAL` gains `configure(const ImuSettings&)`, called before boot; `begin()` fails loud if it
was not. Each chip gets a pure, host-tested function translating `ImuSettings` to its register
values, refusing a combination the chip lacks instead of rounding it: a silently rounded rate is
exactly the mismatch between configured and measured behaviour this probe exists to rule out.
The decode scale comes from the same settings, so it cannot disagree with the range written to
the chip. The product's settings are one named constant in `main.cpp`.

Considered and not taken: a build flag selecting a second set of constants in each driver. It
is less code today, but it leaves the product's settings scattered across two driver files and
the decision of this slice would have to be applied by editing magic register values.

**Probe settings.** ±16 g, the top range on both chips, so a shake is measured, not clipped.
±2000 °/s, which slice 0003's 828 °/s twist peak sat well inside. Output rate: 800 Hz on the
BMI270 (its ODRs are 25 × 2ⁿ) and 1 kHz on the MPU6886 (1 kHz ÷ (1 + divider)), each chip's
fastest rate that a 400 kHz I2C burst read of 12 bytes (about 0.4 ms) keeps up with. Both are
above every candidate product rate, so a lower rate can be simulated from the recording.

**Record, then dump.** On `R` the probe firmware polls the chip's data-ready flag and records a
5 s window into a buffer allocated once at boot (fail loud if the allocation fails), printing
nothing meanwhile, so serial work cannot delay a sample. Each sample is 16 bytes: a `uint32`
timestamp in µs and the six device-frame values as `int16` counts of the configured full scale,
little-endian. Packing the decoded, remapped floats back into counts keeps one decoder (the
host-tested C++ one) instead of a second in the host script; it is exact because each value
started as a count, except +full scale, which clamps one count low. 5 s at 1 kHz is 80 KB, which
the StickC holds once the probe skips the 64 KB canvas; the probe logs the largest free block
at boot so the margin is on record. The reply follows the screen capture's framing (`RBIMU`
header with the byte count, body, `RBEND`), and on the StickC's 115200-baud bridge one window
takes about 7 s to send. The screen shows only the probe's state (ready, recording, sending)
so the operator knows when to move.

**Host side.** `scripts/capture_imu.py` sends `R` and reads one framed window, reusing
`open_port` from `scripts/capture_screen.py` so the port opens without resetting the board.
Its own frame reader is a second copy of the header scan in `read_frame`, accepted under the
rule of three; a third framed reply would extract the shared part. `scripts/imu-probe.mjs`
walks the operator through the protocol, runs the capture per take, and analyses.

**Protocol**, three takes per gesture per board, each a 5 s window:

| Gesture | Class | What the operator does |
|---|---|---|
| `rest` | handling | Board flat on the desk |
| `hold` | handling | Held still in the reading pose |
| `pickup` | handling | Picked up from the desk into the reading pose |
| `setdown` | handling | Put down from the reading pose onto the desk |
| `walk` | handling | Carried in the reading pose while walking |
| `tilt` | handling | Tilted slowly toward each edge in turn, as to slide the dice |
| `bump` | handling | Knocked against the desk edge once, as by accident |
| `shake-gentle` | shake | The softest shake meant as a roll |
| `shake-hard` | shake | A hard dice-cup shake |

**Analysis.** For each take: peak |a|, saturated samples, peak |ω|. For each candidate rate (the
capture rate, then 400, 200 and 100 Hz, each a rate the chip supports, simulated by averaging
blocks of samples as the chip's own decimation filter approximately does) and each detector
feature (peak of ||a| − 1 g|, and its RMS over a sliding window), the worst shake value, the
worst handling value, and the margin between them. The ADR picks the lowest rate and simplest
feature with a clear margin on both boards. Because block averaging only approximates the chip's
filter, the chosen setting is confirmed by a capture at that real setting before the ADR is
accepted. If no feature separates the classes on some board, that is the finding, and the ADR
says so rather than picking a threshold.

## Verification

- **Native (`pio test -e native`)**: the settings-to-register translation for each chip, covering
  every supported value and a refusal for each field; the pack and unpack round trip, including
  both full-scale ends.
- **Python (`scripts/test_capture_imu.py`)**: the frame reader against good, short, mislabelled
  and RBEND-less replies, and the timestamp-gap check.
- **Node (`scripts/imu-probe.test.mjs`)**: block-average decimation, the features and the margin
  computation, on synthetic series with known answers.
- **Device**: `scripts/imu-probe.mjs` on both boards; its summaries and CSVs under
  `artifacts/imu-probe/<env>/` are the evidence the ADR cites. As a probe it is exempt from
  `package.json` wiring (R11). The product firmware with the chosen settings is checked by
  `npm run verify:skeleton` on both boards.

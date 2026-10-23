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

## As built

Shipped on 2026-10-23. The probe's answer is ADR-0005: ±16 g, ±2000 °/s and 100 Hz on both
boards, and a shake told from handling by how long the motion lasts. Both boards' product
firmware runs those settings.

| Definition of Done | Evidence |
|---|---|
| Four envs build with no warnings from `src/` | Six build clean (the four, plus the two 100 Hz probe envs below), each from a clean build |
| Settings translate, or are refused naming the field | `test_imu_settings`: every supported range and rate on both chips, and refusals including rate 0, a non-integer MPU6886 divider and a divider over 255 |
| Unset or refused settings stop boot | `begin()` fails with the reason before touching the bus |
| `R` records silently, then replies `RBIMU … RBEND` | 90 takes captured across both boards; boot logs record the buffer (StickC 78 KB of a 107 KB largest free block) |
| A broken or uneven window fails the capture | `test_capture_imu.py`: header, short body, missing `RBEND`, the board's own failure line, gaps, counter wrap |
| Pack and unpack round trip | `test_imu_wire`: all 65,536 counts through the real decoder, a sign-flipped axis, both full-scale ends; the host flags either end as saturated |
| The probe writes per-take CSVs and the summary | `artifacts/imu-probe/<env>/summary.md` for all four probe envs |
| An ADR records the settings with their numbers, and both boards run them | ADR-0005; `PRODUCT_IMU_SETTINGS` in `src/main.cpp` |
| `verify:skeleton` passes with the raised floor | Both boards passed every check at ±16 g and 100 Hz: shakes measured unclipped at 6.78 g (StickC) and 6.65 g (Cardputer) |

**Deviations from the design.**

- **A fourth feature decided it.** All three planned features (peak deviation, 200 ms RMS, peak
  |ω|) failed on both boards, because an accidental knock out-measured the gentlest shake. A
  small sweep over the StickC's takes picked the busy share (share of the busiest 1 s over
  0.5 g off 1 g); it was fixed in `scripts/imu-probe.mjs` before the Cardputer's takes, which
  then separated without retuning. The numbers are in ADR-0005.
- **The confirmation at the real rate became two envs**, `cardputer-imu-probe-100hz` and
  `m5stickc-plus2-imu-probe-100hz`, recording only the gestures near the threshold (both
  shakes, `pickup`, `setdown`, `walk`, `bump`; 18 takes per board).
- **`ImuHAL` gained `hasNewSample()`.** Recording on a timer of our own would drift against the
  chip's clock and duplicate or skip samples, so the probe waits on the chip's data-ready flag,
  which both drivers already polled privately in `waitForData()`.
- **`FULL_SCALE_COUNTS` moved into `imu_decode.h`**, shared by the decoder and the packer,
  which must scale alike.

**Defects found and fixed.**

- **`delay(n)` could wait under n ms.** Arduino-ESP32's `delay` returns at the nth FreeRTOS
  tick, and on the Cardputer `delay(2)` took 1587 to 2450 µs over 200 calls. The BMI270
  refuses a write sent under 2 ms after its soft reset, so the probe failed boot with
  `PWR_CONF write NACK` in 13 of 14 resets on one evening, and in none earlier that day: it
  depended on where in a tick the call landed. A temporary diagnostic (not committed) showed
  CHIP_ID reads succeeding after 2 ms while the first write was still refused 1 time in 5.
  `src/hal/delay_at_least.h` adds a tick to every wait that stands for a datasheet minimum: the
  BMI270's soft reset and config load, and the MPU6886's reset and gyro start-up (slice 0003's
  fix for the zero first sample had the same weakness). After the fix: 20 of 20 probe boots,
  then 39 of 40 product boots; the one miss was in a loop that did not keep its log, so whether
  the IMU failed or the monitor missed the line is unknown.
- **The first sample of a window was stale.** Between windows nothing read the chip, so its
  data-ready flag was already set when `R` arrived and sample 0 was taken off the chip's grid:
  the first interval read 1407 to 1624 µs in 6 of 8 StickC windows, failing the gap check. The
  probe now reads and drops the waiting sample first; over the next 8 windows the first
  interval was 977 to 1019 µs and the worst anywhere 1165 µs.
- **The analysis could not read its own captures.** Python's `csv` writes `\r\n`, and the
  parser split on `\n` only, so every header failed to match. A regression test with CRLF now
  covers it.
- **A short window lost its explanation.** The Cardputer once delivered 7942 of 8000 bytes.
  The board reports a short write in-line, where the host was still counting body bytes, so the
  reason was discarded; the host now quotes the board's note, or says it sent none. The cause
  of that one short window is unknown; it did not recur in the 15 Cardputer takes that followed.

**Build isolation.** Mid-slice, both product builds failed in the framework itself, with the
Arduino core and the ESP-IDF libraries of different releases. Three local projects pin
pioarduino 55.03.34, and every release installs its framework under the same package names in
the shared `~/.platformio`, so building one swapped Rattlebox's framework out and an
overlapping build compiled against half of each. `platformio.ini` now sets
`core_dir = .platformio` (4.8 GB, gitignored), so no other project can swap it. On this
machine the pipx PlatformIO Core was also pinned to 6.1.19, the version VS Code's extension
runs, so the two stop rewriting each other's state; that is the operator's environment, not
the repo's.

**Fixtures.** Raising the skeleton check's floor to ±16 g made slice 0003's recorded Cardputer
boot (at ±8 g) fail the boot check. Rather than edit a recording, a new StickC boot at
ADR-0005's settings (`scripts/fixtures/skeleton/stickc-boot.log`) carries that check; the old
recording keeps the checks the range does not affect.

**Adversarial review.** A blind correctness pass found three defects, all fixed: the probe's
recording loop had no timeout, so a chip that stopped raising data-ready would hold the board
for good (it now fails after 100 ms without a sample; not reproducible on the host, checked by
three captures after the change); `capture_imu.py` used a backslash inside an f-string
expression, a SyntaxError before Python 3.12 (reproduced with Python 3.11); and a one-sample
take was given an infinite measured rate instead of being refused (regression test).

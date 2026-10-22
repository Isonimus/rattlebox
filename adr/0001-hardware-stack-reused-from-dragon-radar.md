---
id: '0001'
title: "Hardware stack: the Dragon Radar HAL and LovyanGFX, copied in; raw IMU samples on both boards"
type: architecture
status: accepted
date: 2026-10-22
supersedes: []
superseded_by: []
---

## Context

Rattlebox targets two boards:

| | Cardputer ADV | M5StickC Plus 2 |
|---|---|---|
| SoC | ESP32-S3 | ESP32-PICO-V3-02 |
| Display | ST7789, 240×135 | ST7789, 135×240 |
| Input | 56-key keyboard behind a TCA8418 (I2C) | Buttons A and B |
| Audio | Speaker behind an ES8311 codec | Passive buzzer |
| IMU | BMI270 | MPU6886 |

The sibling project Dragon Radar (`../esp32-dragon-ball-radar`) already runs on both
boards. Its stack is a set of abstract interfaces with one folder per board, picked at build
time (esp32-dragon-radar:ADR-0001). It has small in-repo drivers for the ADV's keyboard and
codec (esp32-dragon-radar:ADR-0003) and LovyanGFX with an explicit panel configuration for
each board. On these boards it measured (esp32-dragon-radar:ADR-0012):

| Measured | Cardputer ADV | StickC Plus2 |
|---|---|---|
| Pushing a full canvas to the panel | 13.0 ms | 13.0 ms |
| Free heap with the canvas allocated | 285 KB | 196 KB |

Dragon Radar rejected M5Unified because its `lgfx` namespace conflicts with the LovyanGFX
needed by its custom ESP32-C3 board. Rattlebox has no custom board, so that reason does not
carry over, and M5Unified was reconsidered:

- **M5Unified** drives both IMUs out of the box. The ADV's TCA8418 keyboard would still need
  the M5Cardputer library on top, and none of the numbers above would apply until they were
  re-measured on a different graphics stack.
- **The Dragon Radar stack** has a working keyboard, codec and display path on both boards,
  host-tested where the logic is pure, and the measurements above. It lacks an IMU path:
  the ADV's BMI270 is not driven (a stub reports it unavailable, and Dragon Radar's ledger
  carries this as open work), and the MPU6886 driver integrates a heading instead of
  exposing raw samples.

The IMU is the product's main input, so either option needs IMU work. With the Dragon Radar
stack that work is one driver (BMI270) plus extending another (MPU6886), not a whole stack
to re-verify.

## Decision

- **Copy the Dragon Radar HAL into this repo**: abstract interfaces in `src/hal/`, one
  folder per board under `src/hal/boards/`, chosen by each PlatformIO env's
  `build_src_filter`, statically allocated instances, with no runtime board detection.
  It is a copy, not a shared library. Two consumers do not justify a library, and the two
  projects can diverge without coordinating releases.
- **Two boards only**: `cardputer` (Cardputer ADV) and `m5stickc-plus2`. The original
  Cardputer and the custom C3 board are not supported.
- **Interfaces kept**: display, input, audio, IMU, power and storage. GPS is dropped.
- **The IMU interface delivers raw samples**: acceleration in g and angular rate in °/s, on
  one device frame shared by both boards (x to the screen's right, y to its top, z out of
  the screen). Each board remaps its sensor's axes into that frame. The heading API is
  dropped.
- **The BMI270 driver is written in this repo** over the ADV's shared internal I2C bus. Its
  init sequence and config blob come from Bosch's or M5Stack's published sources, read on
  the day of the port and cited in that slice, as Dragon Radar did for the codec
  (esp32-dragon-radar:ADR-0003). The MPU6886 driver is extended to expose raw samples.
- **Shaking saturates a ±2 g range**, so the accelerometer is configured for at least ±8 g.
  The exact range, output rate and filtering come from an IMU probe and are recorded in
  their own ADR.
- **Same toolchain pins as Dragon Radar**: pioarduino platform 55.03.37 (Arduino core 3.3.7)
  and LovyanGFX 1.2.19, both at exact versions.

## Consequences

- The display, input and audio paths start out working, and Dragon Radar's frame and heap
  figures are a usable baseline. They were measured on Dragon Radar's code, so Rattlebox's
  render probe measures its own frame cost rather than inheriting a number.
- The copied code does not follow fixes made in Dragon Radar, and the reverse is also true.
  A defect found in either is ported by hand.
- Only one board's folder compiles per env, so a change to a shared interface is only
  verified once both envs build (invariant #1 in `CLAUDE.md`).
- The StickC Plus2 has 2 MB of PSRAM, which the generic `esp32dev` board definition does not
  enable. It stays unused until a measurement shows the heap needs it.

---
id: '0020'
title: "The StickC env builds for the Plus 2's 8 MB flash at 80 MHz, with its PSRAM left off"
type: architecture
status: accepted
date: 2026-11-22
supersedes: []
superseded_by: []
---

## Context

The `m5stickc-plus2` env builds for PlatformIO's generic `esp32dev` board, because the platform
ships no definition for the Plus 2 (ADR-0003). Slice 0003's device run showed that everything the
skeleton used boots and runs on it, but never compared the board's flash and PSRAM settings with
the chip's, so that audit went to the ledger.

Measured on 2026-11-22:

| | `esp32dev`, as built | The chip (esptool `flash-id`) | The core's own `m5stack_stickc_plus2` entry (`boards.txt`) |
|---|---|---|---|
| Flash size | 4 MB | 8 MB | 8 MB |
| Partition table | `default`, 1.25 MB per app | | `default_8MB`, 3.2 MB per app |
| Flash clock | 40 MHz (`clock div:2` at boot) | | 80 MHz, as the core's precompiled ESP-IDF assumes (`CONFIG_ESPTOOLPY_FLASHFREQ="80m"`) |
| PSRAM | none on the heap | ESP32-PICO-V3-02, "Embedded PSRAM" | a menu option, off or on |

The product's heap held 111 KB and no PSRAM, and the boot log said nothing about it either way: the
core logs its PSRAM outcome before `Serial.begin`, where the Arduino log goes nowhere. A probe
(`src/probes/psram/`, env `m5stickc-plus2-psram-probe`) asked directly, on two boots:
`psramFound()` was false and ESP-IDF had not started it; started by hand, `esp_psram_init` returned
`ESP_OK` on 2 MB and ESP-IDF's memory test, the one the core runs before using it, passed.

So the chip's PSRAM is sound, and the build leaves it out. The core's `esp32-hal-psram.h` undefines
`CONFIG_SPIRAM` when `BOARD_HAS_PSRAM` is not defined, which is how the Arduino IDE's "PSRAM:
Disabled" works, so its boot-time start (`esp32-hal-misc.c`, `init_psram_new`) is compiled out.
`esp32dev` does not define it.

Turning PSRAM on has two costs on this board. The precompiled core sets
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, so every allocation over 4 KB would come from PSRAM,
the 64 KB canvas included. On the ESP32, PSRAM is slower than internal RAM and DMA cannot read it,
and the StickC is the board that sets the frame budget (ADR-0001). It also sets
`CONFIG_SPIRAM_OCCUPY_HSPI_HOST`, and the display runs on `HSPI_HOST`. The product uses 41 KB of
static RAM.

## Decision

The `m5stickc-plus2` env keeps `board = esp32dev` and overrides it to the chip and to the core's own
Plus 2 entry: `board_upload.flash_size = 8MB`, `board_upload.maximum_size = 3342336`,
`board_build.partitions = default_8MB.csv`, `board_build.f_flash = 80000000L`. `BOARD_HAS_PSRAM`
stays undefined, so the PSRAM stays off.

Rejected:

- **Turning PSRAM on.** Nothing needs it, and it would put the canvas where DMA cannot read it and
  share a host with the display. A feature that needs more heap takes it up in its own ADR, with the
  canvas pinned to internal RAM and the frame cost measured.
- **A board definition of our own** (`boards/m5stickc-plus2.json`). Four overrides in the env say
  the same with less to maintain, and keep the platform's `esp32dev` defaults for everything the
  device run showed works.
- **Fixing only the flash size.** It would leave the flash at half the clock the precompiled core
  was built for, and the 80 MHz clock measurably shortens every frame (below).

## Consequences

- Each app slot grows from 1.25 MB to 3.2 MB; the product's 443 KB image now fills 13% of it,
  down from 34%. NVS stays at `0x9000`, where it was.
- `verify:roll` passed on the StickC after the change: 100 rolls of three dice locked, D6
  chi-square 7.10 (p 0.213) and D20 13.11 (p 0.833). Against the same check that morning on the old
  settings, with the same code, the mean work per frame fell from 6321 to 5463 µs on D6 and from
  6784 to 5938 µs on D20, and the longest from 9785 to 7958 and from 10647 to 9138 µs; no frame
  was late in either run.
- The Cardputer's env needed nothing: its chip reads 8 MB of flash, and its `m5stack-stamps3`
  board declares 8 MB with the `default_8MB` table.
- The PSRAM stays available for later: defining `BOARD_HAS_PSRAM` starts it, and the probe shows
  it works.

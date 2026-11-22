---
id: '0025'
title: "Releases carry an app-only image for OTA installers, and a merged image must hold a bootloader header at its offset"
type: architecture
status: accepted
date: 2026-11-22
supersedes: []
superseded_by: []
---

## Context

ADR-0021 gives each release one image per board: the bootloader, partition table, OTA data and app
merged to flash at 0x0. Two installers the boards' owners use were checked against it, from what
the sibling projects recorded:

- **M5Burner** writes an uploaded firmware at 0x0, as `esptool write-flash 0x0` does. Its catalog
  rejected esp-32-toki-pona's first submission, an app-only image, with "failed to boot the device"
  (esp-32-toki-pona:ADR-0026). Rattlebox's merged image is what it needs: the v0.1.0 images, written
  at 0x0 with esptool 5.2.0, booted both boards to their first line, `[boot] Rattlebox v0.1.0 on
  <board>`, on 2026-11-22.
- **M5Launcher** installs an app over the air into an app slot of its own partition table, so it
  takes the app alone, not a merged image. adversary ships one for it (`-app.bin`, adversary:slice-0014).
  Whether Rattlebox runs from another table's slot depends on what it reads from flash: `src/` uses
  no `esp_partition`, NVS, `Preferences` or filesystem, so nothing it needs is at a fixed partition.

`release.mjs` checks the merged image's bootloader header for DIO by reading byte 2 at the
bootloader's offset. It does not check that a header is there. A classic ESP32 image is erased flash
(`0xff`) from 0x0 to its bootloader at 0x1000, so the check passes only because PlatformIO's
metadata gives the right offset; read at a wrong one, a stray `0x02` would pass for DIO.
esp32-wifi-sapper:ADR-0095 checks the image magic, `0xE9`, there too.

## Decision

- **Each release also carries `rattlebox-<version>-<board>-app.bin`**, the build's `firmware.bin`
  unchanged, for OTA installers such as M5Launcher. It is refused unless it opens with an ESP image
  header. `SHA256SUMS.txt` covers it.
- **The merged image keeps its name**, `rattlebox-<version>-<board>.bin`, and stays the one for
  esptool, M5Burner and web flashers. Considered and not taken: renaming it `-factory.bin` as the
  siblings do. The `-app` suffix already tells the two apart, and the name v0.1.0 shipped and the
  README shows stays valid.
- **A merged image is refused unless an ESP image header (`0xE9`) starts at the bootloader's offset**
  and names DIO. CI's `--check-image` runs both checks on every push, and both images.
- **Listing Rattlebox in the M5Burner catalog is the maintainer's act**, from an M5Stack account, as
  in adversary:slice-0014. The release's merged images are what it is given; CI does not submit.

## Consequences

- A firmware change that comes to read a partition, NVS or a filesystem must check that it still
  runs from an M5Launcher slot, or stop shipping the app image. That is review-only: nothing tests an
  install from another partition table.
- The app image has not been installed through M5Launcher on a board; `LEDGER.md` carries it.
- v0.1.0 has no app image; the next release is the first with one.

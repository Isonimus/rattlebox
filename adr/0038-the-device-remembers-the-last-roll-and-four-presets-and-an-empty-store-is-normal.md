---
id: '0038'
title: "The device remembers the last roll and four presets in NVS, and an empty store is a normal state"
type: architecture
status: accepted
date: 2026-11-28
supersedes: []
superseded_by: []
---

## Context

`docs/roadmap.md` asks v0.2 for presets and roll history in NVS, and for the StickC to cycle saved
presets since it cannot type an expression. Nothing in `src/` has read or written persistent storage
before this decision.

Three things make presets worth more than convenience on the StickC. ADR-0036 made the modifier
settable only by a typed expression, so a board with two buttons has no way to reach `3d6+2` at all;
`BOOT_CHOICE` in `src/roll/dice_choice.h` is a compile-time `D20`, so an expression typed on the
Cardputer is lost at the next power cycle; and the tray's controls step the type and the count one
at a time, so returning to a particular roll costs several presses even when the device is still on.

**ADR-0025 set a condition on exactly this change**: a firmware change that comes to read a
partition, NVS or a filesystem must check that it still runs from an M5Launcher slot, or stop
shipping the app image. It recorded that as review-only. The check was performed by reading the
Launcher's own sources rather than by guessing:

- Every starting partition table the Launcher ships carries the same NVS row Rattlebox already
  builds against, `nvs, data, nvs, 0x9000, 0x5000`: `custom_4Mb.csv`, `custom_8Mb.csv`,
  `custom_8Mb2.csv` and `custom_16Mb.csv` agree on the offset and the size, and so does the stock
  `default_8MB.csv` this firmware's `board_build.partitions` names.
- The Launcher generates a table sized to the app it installs, and `partition_install_layout.cpp`
  creates and resizes data partitions **by label**, never naming `nvs`, so an existing NVS entry is
  carried through an install untouched. `partitioner.cpp` does not relocate it either.
- `partitioner.cpp` offers the user a "Format NVS Partition" menu item, and the Launcher keeps its
  own boot state in that same 20 KB partition.

Two measured facts about the platform settle questions that would otherwise have been designed
around:

- **An unchanged write costs no flash write.** `Storage::writeItem` compares the stored value
  against the new one and returns `ESP_OK` without writing, for blobs through `cmpMultiPageBlob`
  and for scalars through `cmpItem`. Its own comment says it is "cheaper to purposefully not write
  out new data, since it may invoke an erasure of flash". So no debounce, idle timer or write
  budget is needed to keep a store that follows every change off the flash. This was read at
  ESP-IDF commit `87912cd291`, which is what the build's own `IDF_VER` names; note that
  `esp_idf_version.h` in the same package reports 5.5.1 while `IDF_VER` says
  `v5.5.2-729-g87912cd291`, so the header is not the version to cite.
- **The core initialises NVS before `setup()`.** `esp32-hal-misc.c` calls `nvs_flash_init()` and, on
  `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND`, erases the whole NVS partition and
  re-initialises. If that fails it calls `log_e` and carries on. The app has therefore always
  initialised NVS by linking the core, and ADR-0025's claim that `src/` uses no NVS was true of
  `src/` without being a guarantee that the app left the partition alone. What changes here is that
  Rattlebox comes to depend on the partition's contents, and that a failure to initialise it is
  invisible from inside `setup()`.

The band has no room for a permanent indicator. Laid out as `drawResultsBand` draws it, a
two-character slot label is 24 px, which puts the worst band at 250 px of the 240 the screen has:
the same 10 px overflow ADR-0036 weighed and refused to buy for a second modifier digit.

## Decision

- **One versioned record, in one NVS blob**, under one namespace of this firmware's own. It holds
  the choice last rolled and four preset slots, each slot a `DiceChoice`. One blob is one NVS entry,
  so one save is one compare-and-write however many fields changed.

- **The last choice is written whenever the choice changes**, and boot restores it in place of
  `BOOT_CHOICE`. No debounce and no write budget, because an unchanged write reaches no flash, as
  measured above. `BOOT_CHOICE` stays as the value an empty store boots to.

- **Four preset slots, reached by the two holds**, which were inert on both boards before this
  decision (`applyAction` fell them through to no change). `LONG_PRIMARY` steps to the next saved
  slot and rolls it; `LONG_SECONDARY` saves the current choice. Four is chosen against the control,
  not against the storage: a preset is three bytes in a 20 KB partition, so the cost that bounds the
  count is how many holds it takes to come back around with nothing on screen listing the slots.

- **An absent, short, wrong-version or invalid record is an empty store, and an empty store is
  normal.** The Launcher offers the player a "Format NVS Partition" command and keeps its own state
  in the same partition, so a wiped store is a state the device is reached in on purpose rather than
  a fault. Boot in that case uses `BOOT_CHOICE` and says so on the serial port, and does not stop.

- **A record out of flash is untrusted and validated field by field before it reaches the choice**:
  the type must be a catalog entry, the count between 1 and that type's `maxDiceOf`, the modifier
  within `MODIFIER_LIMIT`. A record that fails anywhere is discarded whole rather than repaired,
  because a repaired record is a choice the player never made. This is what keeps a store written by
  another version, or by another firmware in the same partition, from reaching code whose invariants
  it does not satisfy.

- **A store that cannot be opened or written is loud.** The distinction the previous point does not
  blur: *no record* is normal and silent past one line, while a storage layer that fails to open its
  namespace or returns an error from a write is a failure reported on the serial port. The boot step
  fails when the namespace cannot be opened, because the core's own initialisation failure is only
  `log_e`'d and would otherwise reach the product as presets that silently never persist.

- **Feedback is a transient band message, never a permanent indicator**, on the pixel measurement
  above. The band already draws a message in place of its contents for the expression field
  (ADR-0036), and that is the mechanism a recall and a save report through.

- **The app image keeps shipping.** ADR-0025's condition is met by the tables recorded above: NVS is
  at the same offset and size under the Launcher as under the stock table, and an install does not
  move it. This discharges the condition for NVS only. It says nothing about `esp_partition` or a
  filesystem, and it is not the install audit, which stays open on `LEDGER.md`: reading a table is
  not the same as installing through the tool that writes it.

- **Roll history is not part of this decision.** It is the other half of the roadmap line and a
  different shape: an append log with its own wear profile, and no place on a 240x135 screen that
  already holds the tray and the band. It gets its own slice.

## Consequences

- A persisted record is validated against the catalog before it reaches the choice, for every
  future record and not only this one. That is a rule later work must follow, so it enters
  `CLAUDE.md` §4 in this commit.

- Reading a preset is no longer a compile-time constant, so the two holds now do something on both
  boards. A future control that wants a hold must take it from presets or find another gesture.

- The StickC reaches a modifier for the first time, by recalling a preset a Cardputer typed or by
  saving one that a previous session's expression set. It still cannot compose one.

- Persistence across a power cycle cannot be asserted in a unit test, so this ships a verify script
  that drives the device and resets it between writes and reads.

- A player who formats NVS from the Launcher loses the presets and the remembered roll, and the
  device boots to `BOOT_CHOICE` without complaint. That is deliberate, and it is the reason the
  empty case is not a failure.

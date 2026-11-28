---
id: '0039'
title: "Presets and the remembered roll"
type: slice
status: accepted
date: 2026-11-28
supersedes: []
superseded_by: []
---

## Goal

The device forgets everything the moment it is unplugged. `BOOT_CHOICE` is a compile-time `D20`, so
an expression typed on the Cardputer dies with the session, and on the StickC, which ADR-0036 left
with no way to compose a modifier at all, there is no route to `3d6+2` in the first place. This
slice gives the firmware a memory: the roll it last threw comes back at boot, and four preset slots
sit behind the two holds that were inert on both boards, one to recall and one to save. The store is
a single versioned record in NVS, validated field by field on the way in, and an empty one is a
normal state rather than a fault, because the Launcher hands the player a command that wipes it
(ADR-0038).

It delivers the first half of the roadmap's "presets and roll history, stored in NVS". The history
half is deliberately left out: it is an append log with its own wear profile and no place on a
screen that already holds the tray and the band, so it gets its own slice, and this one closes no
ledger item because the roadmap line is not tracked there.

## Definition of Done

- **Given** a device whose store is empty, which is what a formatted NVS gives
- **When** it boots
- **Then** it comes up on `BOOT_CHOICE`, one D20 with no modifier, says on the serial port that
  there was nothing to restore, and does not stop

---

- **Given** a Cardputer that has just rolled a typed `3d6+2`
- **When** it is reset and boots again
- **Then** the band shows `D6+2`, three dice are what the next shake throws, and the serial port
  reports the restored choice

---

- **Given** any choice on either board
- **When** the player holds the secondary control
- **Then** the choice is saved into a slot, the band says so in a message that fits its 240 px, and
  the message gives way to the band's usual contents

---

- **Given** at least one saved slot
- **When** the player holds the primary control
- **Then** the next saved slot becomes the choice, the dice are thrown for it, and holding again
  moves on and comes back around after the fourth

---

- **Given** a store holding a record that is short, of another version, or whose fields are out of
  range, such as a count above the type's `maxDiceOf` or a modifier past `MODIFIER_LIMIT`
- **When** it boots
- **Then** the whole record is discarded rather than repaired, boot uses `BOOT_CHOICE`, and no
  invalid choice ever reaches the tray

---

- **Given** a storage layer whose namespace cannot be opened
- **When** boot runs its steps
- **Then** that step fails with a reason, exactly as a failed display or IMU does, because the
  core's own NVS failure is only logged and would otherwise reach the product as presets that
  silently never persist

---

- **Given** the StickC, which cannot type and had no way to reach a modifier
- **When** it recalls a slot a Cardputer's expression saved
- **Then** it rolls with that modifier and the band shows it

## Design

### A pure store, a thin HAL, and nothing about dice below the seam

The decisions about what a record means live in a pure module, `src/roll/preset_store.{h,cpp}`,
which the host tests cover by including its `.cpp` the way every other suite does. It owns the
record layout, the version, the validation and the recall cycle, and it never touches hardware.

Underneath it, `src/hal/storage_hal.h` declares a `StorageHAL : Startable` with one record to read
and one to write, as bytes and a size. It knows nothing about dice, which is what keeps the catalog
out of the HAL and the flash out of the tests. The implementation, `src/hal/nvs_storage.{h,cpp}`,
is shared by both boards rather than duplicated per board: both are ESP32 and NVS is identical on
them, so it sits outside `src/hal/boards/` and each board's `getHAL()` references one static of it.
That puts it under invariant 1, which is the correct place for it: a change there has to build for
both boards.

`begin()` opens the namespace and fails if it cannot. It does not call `nvs_flash_init`, because the
Arduino core already did before `setup()` ran, and it does not erase the partition on any error: the
Launcher keeps its own boot state in those same 20 KB (ADR-0038).

### The record is validated, not repaired

A record read back is untrusted: the partition is shared with the Launcher, a previous firmware may
have written another layout, and the player can format it. Validation checks the version, the length,
and then every field of every choice against the catalog, and one bad field discards the entire
record. Repairing it would put a choice in front of the player that the player never made, and
clamping a count to `maxDiceOf` is exactly the silent reduction ADR-0036 refused for a typed
expression.

### One blob, one write

The whole record is one NVS blob under one key, so a save is one compare-and-write whatever changed.
No debounce and no idle timer: ESP-IDF compares before writing and an unchanged record reaches no
flash at all (ADR-0038 records the source and the version), so following every change is already the
cheap option.

### The holds, and what the band says

`applyAction` fell `LONG_PRIMARY` and `LONG_SECONDARY` through to no change on both boards, so both
are free. The primary hold recalls and rolls, the secondary hold saves. Neither becomes a mode:
there is nothing to be inside and nothing to leave, which matters because the band has no pixels to
tell the player which mode they are in, and a two-character slot label would overflow it by the same
10 px ADR-0036 refused to buy.

What a save or a recall did is said in a transient `messageBand`, the same mechanism the expression
field draws its refusals through, and it gives way to the band's usual contents on the next frame
that has something else to say.

### What existing code was considered and not reused

- **`Preferences`**, the Arduino wrapper over NVS, was considered and not used. It reports failure as
  a boolean and swallows the `esp_err_t`, so a write that failed cannot say why, which is the
  opposite of what the quality bar asks of a boundary. The raw `nvs_*` calls are no larger here and
  carry `esp_err_to_name`.
- **`ExpressionEntry`** was considered as the home for the transient message and not used. It is the
  typed field's state machine and would have grown a second, unrelated reason to exist; the message
  is built straight from `messageBand`, which both already use.
- **`parseDieType` and `maxDiceOf`** are reused for validation, exactly as `parseNotation` reuses
  them, so the catalog stays the one place that knows what a type permits.
- **`BOOT_CHOICE`** is kept rather than deleted. It stops being what the device always boots to and
  becomes what it boots to with nothing stored, which is a value the empty case still needs.
- **The per-board HAL pattern** was considered for storage and deliberately not followed: there is
  no board-specific behaviour to express, and two copies of one NVS implementation would be the
  duplication invariant 1 exists to catch.

## Verification

- `test/test_preset_store` covers the pure module: an empty store boots to `BOOT_CHOICE`; a round
  trip returns what was written; a short record, a wrong version, an out-of-range count, an
  out-of-range modifier and an unknown type are each discarded whole; saving fills slots in order;
  recall cycles through the saved slots and comes back around; recall with nothing saved changes
  nothing. This proves every Definition of Done scenario about the record's contents, including the
  invalid-record scenario, which is the one no device run can produce on purpose.
- `test/test_dice_choice` is unchanged, and that is the point: `applyAction` still returns "no
  change" for both holds, because what a hold does is a preset and not a step of the choice. The
  holds are wired in `main.cpp` over the pure store, so the decisions they make are
  `test/test_preset_store`'s and the one line each in `main.cpp` is the verify script's.
- `test/test_preset_store` also pushes every message a hold can show through `messageBand`, which
  aborts on text it cannot draw, so the scenario about the message fitting 240 px is asserted
  beside the store that produces it rather than in the band's own suite.
- `test/test_serial_commands` is extended for the two new command bytes, since a hold cannot be
  expressed as a press over a serial port and so needs one of its own.
- `scripts/presets-verify.mjs`, wired into `package.json` as `verify:presets`, drives a real board
  over the serial port: it sets a choice, resets the device, and checks the boot line reports the
  restored choice, then saves a slot, resets, recalls it and checks the roll that follows. Boot
  persistence across a reset is the one claim no unit test can make, which is why it is here. It
  fails on any error line, and writes its captures under `artifacts/presets-verify/<env>/`.

## As built

Every Definition of Done scenario was met.

- **An empty store boots to `BOOT_CHOICE` and says so.** Proven on hardware rather than only on the
  host: the StickC's first run reported `presets-nothing`, because nothing had ever written this
  namespace, and the device came up on one D20 without stopping.
- **A typed choice came back after the power was cut.** `3d6+2` was typed, the board was reset, and
  it booted on `restored 3 D6 mod +2, 0 saved` and threw three D6.
- **A choice changed by button came back too.** PRIMARY stepped the D6 to a D8, carrying the
  modifier, and the boot after the next reset reported `restored 3 D8 mod +2`.
- **A hold saved and a hold recalled.** The secondary hold reported `saved as 1`; after two resets
  the primary hold reported `recalled 1: 3 D6 mod +2` and threw it. Both captures show the band
  legible at `saved as 1` and `preset 1`.
- **An invalid record is discarded whole**, which no device run can stage on purpose, so it is
  `test/test_preset_store`'s: a short record, another version, a count above the type's
  `maxDiceOf`, a modifier past `MODIFIER_LIMIT`, a type outside the catalog, each cursor out of its
  bound, cursors that disagree with each other, and a bad field in an occupied slot.
- **A store whose namespace will not open fails the boot step**, as a failed display or IMU does,
  because it is an ordinary `Startable` in the boot list.
- **The StickC reached a modifier**, which it had no way to compose, by recalling one.

### What the adversarial pass found

One reviewer, on Sonnet, briefed blind to intent with the diff, `CLAUDE.md` and `adr/INDEX.md`. It
stated plainly that it only read and ran nothing. Four findings, all four accepted and fixed:

1. **The verify script could not have passed on any board.** `setup()` throws a roll after every
   boot, and the plan counted launches without expecting those three. The host fixture encoded the
   same wrong model, so the unit tests were green while the device script could not pass. Fixed by
   expecting the boot roll and checking the choice it throws, which turned the defect into a
   stronger claim: a restored choice has to reach the tray and not merely the log. A test now pins
   it, failing with `expected 12 reported steps, saw 9` when the boot rolls are taken out.
2. **Cursors were validated one at a time and not against each other.** A record with
   `savedSlots = 1` and `nextSave = 3` passed every bound; the next save then left slot 1
   unoccupied below `savedSlots`, and a later recall returned an all-zero choice and launched a
   roll of no dice, writing it to flash on the way. Fixed by requiring `nextSave == savedSlots`
   while the slots are still filling, with the wrapped case left free. The regression fails
   `Expected FALSE Was TRUE` without the check.
3. **`test_the_recall_cursor_survives_a_round_trip` asserted nothing.** It expected slot 0, which is
   also `emptyPresets()`'s default, so it passed whether or not the cursor was serialised at all.
   Rewritten to expect slot 1; with `recallAt` dropped from `writePresets` it now fails
   `Expected 1 Was 0`, where the old one passed.
4. **A failed read was followed by writes that destroyed the record.** A transient read error left
   the store reported as broken and then written over by the first button press, turning one failed
   read into lost presets. Fixed: a failed read clears `presetsWritable` for the session and the
   boot line says `and nothing will be written over it this session`. An empty store and a refused
   record stay writable, because those are states the firmware knows.

The reviewer also noted that the comment above `restorePresets()` claimed it runs after the ready
line when it runs before. It was right, and the comment now says what the code does; the order
matters, because a host that waited for the ready line relies on the store having already reported.

### Corrections made before the review

- **The modifier's bound was in the wrong header.** `MODIFIER_LIMIT` lived in `dice_notation.h`, so
  `roll_band.cpp` already included the grammar's header for a bound on `DiceChoice::modifier`, and
  the store would have been the second to do it. It moved to `dice_choice.h` beside the field it
  bounds, and `test_results_band` no longer needs the notation module at all.
- **The ADR cited the wrong ESP-IDF.** The compare-before-write evidence was first read at tag
  v5.5.1, the version `esp_idf_version.h` reports, while the build's own `IDF_VER` names
  `v5.5.2-729-g87912cd291`. Re-read at that commit, where the behaviour is the same, and ADR-0038
  now cites the commit and warns that the header is not the version to cite.

### What it cost

Against `dcb5e2a`: Cardputer RAM 54,084 to 54,276 and flash 498,851 to 507,803; StickC RAM 53,520
to 53,728 and flash 453,699 to 463,031. Most of the flash is the NVS library, linked for the first
time.

### What was checked

`npm test` 542 cases (245 node, 297 PlatformIO), zero failures. `npm run lint` 0 errors and 0
warnings over 39 documents, `npm run index` regenerated. `npm run build:envs` 13 of 13, and both
firmware builds carry no warnings from `src/`. On a real M5StickC Plus 2 on `/dev/ttyACM0`:
`npm run verify:presets` passed, and `npm run verify:notation` was re-run and passed, because this
slice changed the typed-expression path.

No Cardputer was on a port, so the holds have never been pressed as keys on that board, only driven
through its serial port; `LEDGER.md` already carries the item for typing on a real Cardputer and
this adds nothing new to it. There is no mutation harness in this repo, so that gate did not apply.

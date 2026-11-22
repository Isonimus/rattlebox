# Ledger

The single mutable file in this repo (stele:ADR-0001). Everything else is
immutable or generated. Open work, deferrals, and known defects all live here — there is
no second tracking file, because two files require manual sync and manual sync does not
happen.

**Closing an item means deleting its line here.** Do not annotate the source ADR; the
ADR's claim ("at the time of this decision we deferred X") stays true forever and needs
no update. Single writer, one direction.

Format: `- [type] description (ADR-NNNN)` — type is `bug` | `feature` | `deferred` |
`audit`. Cite the source ADR where one exists; rule 8 checks that the citation resolves.
A decision that lives in **another** repo is cited `<repo>:ADR-NNNN` — the linter cannot
open that corpus, so it skips qualified references (stele:ADR-0009).

## Open

<!-- One line per open item. Delete the line to close it; the git log is the done-record. -->
- [deferred] No CI builds both envs yet, so invariant #1 holds by review alone (ADR-0001)
- [deferred] No binary release exists yet; the first one must meet the binary-distribution conditions in `docs/third-party-notices.md` (Bosch and LovyanGFX notices bundled, LGPL relinking for the Arduino core) (ADR-0001)
- [audit] The `m5stickc-plus2` env builds for the generic `esp32dev` variant; its flash size and PSRAM settings were never compared with the Plus 2's, only shown to boot and run the skeleton (ADR-0003)
- [bug] One handheld StickC boot failed `imu FAILED: no ACK at 0x68` (ESP-IDF error 259) on 2026-10-23; it did not recur in over 300 desk resets or two handheld runs, so the cause is unknown. It recurred on 2026-11-22 on the desk, on the first reset `verify:roll` made right after a flash: `i2cWriteReadNonStop(): i2c_master_transmit_receive failed: [259] ESP_ERR_INVALID_STATE` at 308 ms, then no `[boot] imu OK`; the next reset booted and rolled 100 times (ADR-0003)
- [deferred] The tray's gravity leaves out the rotating frame's centrifugal, Euler and Coriolis terms from the gyro; adding them would stir the dice more, so the fairness test reruns if they are (ADR-0010)
- [deferred] The three-dice fairness test (`npm run test:fairness`, 208 s on 2026-11-22) runs only by hand before a physics or input change merges; no CI runs it, so invariant #2's three-dice half holds by review (ADR-0010)
- [audit] The fairness test's shakes are twelve takes from one operator on one day; record other people's shakes (slower, rotational, one-handed) and rerun the probe's shake sweep before a release (ADR-0010)
- [audit] The shake detector's margin (0.07 each side) rests on 3 takes per gesture from one operator; re-run `scripts/imu-probe.mjs` with other people's shakes and handling before relying on it in a release (ADR-0005)
- [audit] One of 40 Cardputer product boots at ADR-0005's settings, after the `delayAtLeastMs` fix, showed no `[boot] imu OK` line within 5 s on 2026-10-23; its log was not kept, so an IMU failure and a monitor that missed the line after USB re-enumeration are both open (ADR-0005)
- [bug] Once every die locks, `ShakeRoll` takes the level from the latest still span, however old: if the hand never holds within 0.1 g for 0.25 s between the shake and the last lock, the level snaps back to the hold before the shake and the resting dice slide. It never happened in 120 replayed upright rolls (dice lock 1.9 s after detection on average, and the jump at lock was at most 8.1 deg, median 2.6 deg, against the 24 deg recline); a fix would take only a still span found since the roll took off (ADR-0014)
- [audit] The one-die fairness test (`npm test`) took 146 s when slice 0012 shipped and about 325 s on 2026-11-22, on slice 0013's commit as well as with slice 0015, so slice 0015 did not cause it; find whether slice 0012, slice 0013 or the machine slowed it (ADR-0010)
- [deferred] On-device checks (`*-verify.mjs` device halves) need a board on the port and no hardware-in-the-loop runner exists, so CI runs only their parsing tests (ADR-0003)

## Resolved

Entries move out of "Open" by deletion. A narrative of what merely *happened* belongs in
the git log; what belongs on an open item is evidence it cannot be re-derived later — a
measured result, a reproduction, a count the eventual decision turns on. Keep that with
the item while it is open, and move it into the ADR that closes it. What this file is not
is a record of completed work.

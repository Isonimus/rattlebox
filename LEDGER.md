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
- [bug] One handheld StickC boot failed `imu FAILED: no ACK at 0x68` (ESP-IDF error 259) on 2026-10-23; it did not recur in over 300 desk resets or two handheld runs, so the cause is unknown (ADR-0003)
- [feature] The shake detector, tray gravity and spin at detection are built and host-tested but not on the device; the roll slice wires them into the product and captures level from the holding angle at the start of each roll, settling which samples count as that angle (ADR-0010)
- [deferred] The tray's gravity leaves out the rotating frame's centrifugal, Euler and Coriolis terms from the gyro; adding them would stir the dice more, so the fairness test reruns if they are (ADR-0010)
- [deferred] The three-dice fairness test (`npm run test:fairness`, 88 s) runs only by hand before a physics or input change merges; no CI runs it, so invariant #2's three-dice half holds by review (ADR-0010)
- [audit] About one roll in nine is detected more than once, the gentle shakes hovering near the 0.23 share, and each re-detection kicks the dice again partway through the shake; the roll slice checks whether that shows as a stutter (ADR-0010)
- [audit] The fairness test's shakes are twelve takes from one operator on one day; record other people's shakes (slower, rotational, one-handed) and rerun the probe's shake sweep before a release (ADR-0010)
- [audit] The shake detector's margin (0.07 each side) rests on 3 takes per gesture from one operator; re-run `scripts/imu-probe.mjs` with other people's shakes and handling before relying on it in a release (ADR-0005)
- [audit] One of 40 Cardputer product boots at ADR-0005's settings, after the `delayAtLeastMs` fix, showed no `[boot] imu OK` line within 5 s on 2026-10-23; its log was not kept, so an IMU failure and a monitor that missed the line after USB re-enumeration are both open (ADR-0005)
- [feature] The product's frame loop is not built: two canvases with the DMA push overlapping the next frame's drawing, paced to 60 fps; the roll slice builds it and checks whether an unsynchronised push tears (ADR-0007)
- [audit] With the second canvas, the StickC's largest free DMA block falls from 110,580 to about 45,000 bytes, measured with only the display up; the heap with audio, physics and the IMU running beside both canvases is unmeasured (ADR-0007)
- [feature] The physics is not yet in the product: the roll slice runs four 1/240 s steps per frame, launches from `esp_random()` with an entropy source on, nudges cocked dice, measures a step's cost on the StickC, checks whether a speculative contact's early bounce (up to about 15 mm) shows, and shows each locked result, since about 1 D20 in 500 in a three-die roll is knocked onto another face after it locks (ADR-0009)
- [audit] Host GCC 13.3 miscompiled the launch at -O2 through modref; the firmware's GCC 14.2 looked right in its modref dumps but has never run the physics, so a device capture must show locked results matching the face up (ADR-0009)
- [deferred] On-device checks (`*-verify.mjs` device halves) need a board on the port and no hardware-in-the-loop runner exists, so CI runs only their parsing tests (ADR-0003)

## Resolved

Entries move out of "Open" by deletion. A narrative of what merely *happened* belongs in
the git log; what belongs on an open item is evidence it cannot be re-derived later — a
measured result, a reproduction, a count the eventual decision turns on. Keep that with
the item while it is open, and move it into the ADR that closes it. What this file is not
is a record of completed work.

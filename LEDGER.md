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
- [deferred] The tray's gravity leaves out the rotating frame's centrifugal, Euler and Coriolis terms from the gyro; adding them would stir the dice more, so the fairness test reruns if they are (ADR-0010)
- [feature] A roll trigger a brisk walk cannot fire, such as a shake that must start from holding still or a button held while shaking; it needs a probe protocol of walk, stop and shake, and could also catch the slow rocking shakes the busy share misses (ADR-0024)
- [feature] A browser web flasher on GitHub Pages for the release images, so a board can be flashed without installing esptool (ADR-0021)
- [audit] Install a release's `-app.bin` through M5Launcher on both boards: the app runs from another partition table's slot only because it reads no partition, NVS or filesystem, and no install has shown it yet (ADR-0025)
- [audit] A release's `NOTICES.md` carries ESP-IDF's `COPYRIGHT.rst` component list, not each linked component's own license text; establish which components the firmware links and carry their texts (ADR-0021)
- [bug] The fairness probe splits flat dice from propped ones by the widest ratio between neighbouring final tilts, not by whether the roll actually locked or was nudged, and now that `checkTiltGap` fails on that split a die whose tilts happen to cluster differently could fail a healthy run; the probe already knows which rolls were nudged, so the split could be measured rather than inferred (ADR-0033)
- [audit] Type an expression on a Cardputer ADV, which no run has done: the notation keys' printed positions are placed from the keycaps and from the four positions ever confirmed rather than from a press (the driver prints `[kbd] down row R col C` for every key it reads), and the keyboard's ordered input queue is host-tested only, since `scripts/notation-verify.mjs` types through the serial port and not through the keys (ADR-0036)
- [feature] A notation modifier above +9, which needs the band to find 10 px: laid out as `drawResultsBand` draws it, `3d20+99 = 159` ends at 250 px of the 240, and the two cheap ways to buy the space (dropping the total's `= `, halving the 6 px segment gap) were weighed and refused (ADR-0036)
- [feature] Keep and drop in the notation (`2d20kh1`, `4d6dl1`): `4d6dl1` needs a fourth body, so the count cannot be raised until the render probe has been run at four dice on the Cardputer, whose worst three-D12 frame was already 15,222 us of 16,667; when it ships the band shows every result with the dropped ones dimmed (ADR-0036)
- [deferred] On-device checks (`*-verify.mjs` device halves) need a board on the port and no hardware-in-the-loop runner exists, so CI runs only their parsing tests (ADR-0003)

## Resolved

Entries move out of "Open" by deletion. A narrative of what merely *happened* belongs in
the git log; what belongs on an open item is evidence it cannot be re-derived later — a
measured result, a reproduction, a count the eventual decision turns on. Keep that with
the item while it is open, and move it into the ADR that closes it. What this file is not
is a record of completed work.

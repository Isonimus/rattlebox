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
- [feature] The host fairness test (invariant #2) does not exist yet; the M0 fairness probe becomes it (ADR-0002)
- [feature] No regression test yet asserts that a locked result never changes (invariant #3) (ADR-0002)
- [deferred] No binary release exists yet; the first one must meet the binary-distribution conditions in `docs/third-party-notices.md` (Bosch and LovyanGFX notices bundled, LGPL relinking for the Arduino core) (ADR-0001)
- [deferred] Slice 0003 awaits its M5StickC Plus2 device check (`npm run verify:skeleton -- --env m5stickc-plus2`), which sets `STICKC_AXIS_MAP`, exercises the esptool reset through the CH9102 and the GPIO 37/39 buttons; its As built is written after it. The Cardputer ADV passed on 2026-10-22 with the identity axis map: rest 0.999 g at 50 Hz, noise std dev at most 0.0011 g and 0.074 dps, twist peak 569 dps, shake peak 4.1 g (ADR-0003)
- [deferred] On-device checks (`*-verify.mjs` device halves) need a board on the port and no hardware-in-the-loop runner exists, so CI runs only their parsing tests (ADR-0003)

## Resolved

Entries move out of "Open" by deletion. A narrative of what merely *happened* belongs in
the git log; what belongs on an open item is evidence it cannot be re-derived later — a
measured result, a reproduction, a count the eventual decision turns on. Keep that with
the item while it is open, and move it into the ADR that closes it. What this file is not
is a record of completed work.

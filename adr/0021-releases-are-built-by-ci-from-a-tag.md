---
id: '0021'
title: "Releases are built by CI from a version tag, as one image per board that flashes at 0x0"
type: architecture
status: accepted
date: 2026-11-22
supersedes: []
superseded_by: []
---

## Context

v0.1's exit criteria are met, and no binary has been published. `docs/third-party-notices.md`
lists what a binary release must carry: the Bosch and M5Unified notices, LovyanGFX's notices,
ESP-IDF's license, and, for the Arduino core's LGPL-2.1, a way for the recipient to relink the
firmware against a modified core. The ledger held that work (ADR-0001).

A board shows nothing of the build it runs: no log line or screen names a version, so a bug report
could not say which build it came from.

The repository is public on GitHub (`Isonimus/rattlebox`), as of 2026-11-22.

## Decision

- **Version.** A release is a git tag `v<major>.<minor>.<patch>`. Before every firmware build,
  `scripts/pio_firmware_version.py` writes `git describe --tags --match 'v[0-9]*' --always --dirty`
  into a generated `firmware_version.h`, and the product's first boot line is
  `[boot] Rattlebox <version> on <board>`, before any part starts, so a failed boot names its build
  too. A header, not a `-D` flag: a flag that changes with every commit changes every compile
  command and rebuilds the whole firmware, where the header, rewritten only when the version
  changes, rebuilds what includes it. A build from a source archive has no git history, so it names
  its version with the `RATTLEBOX_VERSION` environment variable, or stops and says how; it never
  invents one.
- **Built by CI.** Pushing a tag runs `.github/workflows/release.yml`, which builds both boards from
  the tagged source, packages them with `scripts/release.mjs` and creates the GitHub release. The
  binaries then come from the public source at the tag, not from one machine's caches, which is
  what makes the source offer true. A tag can be pushed on any commit, so the job first refuses a
  tag that is not on `main`, and runs `npm test` and the three-dice fairness test itself.
- **One image per board.** `rattlebox-<version>-<board>.bin` merges the bootloader, partition table,
  OTA data and app at the offsets PlatformIO reports for the env, with the app at the offset of the
  partition the bootloader starts (factory, else `ota_0`), read from the built `partitions.bin`. It
  flashes at 0x0 with `esptool write-flash 0x0`.
- **Refused images.** `release.mjs` fails rather than package an image whose firmware was stamped
  with another version than the tag (a dirty or untagged build), or whose bootloader header is not
  DIO. The boot ROM reads the bootloader in its header's mode before it can switch to quad, so a
  QIO header boot-loops the board, and merging, unlike PlatformIO's uploader, does not rewrite it
  (adversary:slice-0014). Both bootloaders build DIO today, although the Cardputer's board
  definition names QIO; the merge asks for DIO and the check holds it there if a platform update
  changes that.
- **Notices.** Each release carries `NOTICES.md`: the source offer at the tag, Rattlebox's license,
  `docs/third-party-notices.md`, LovyanGFX's `license.txt` from the build, and the Arduino core's
  LGPL-2.1, ESP-IDF's Apache-2.0 and ESP-IDF's `COPYRIGHT.rst` component list, vendored unmodified
  in `docs/licenses/` from the upstream tags the build uses (Arduino core 3.3.7, ESP-IDF
  `87912cd291`), since neither package ships them. And `SHA256SUMS.txt`.

Rejected:

- **Releasing from this machine with `gh release create`.** Faster to a first release, but the
  binaries would come from local caches, with nothing to show they match the published source.
- **Shipping only `firmware.bin`.** Flashing it needs the bootloader, partition table and their
  offsets for each board; one image at 0x0 is what a web flasher and a one-line esptool command
  take.
- **A web flasher in v0.1.** It needs a manifest per board and a Pages deploy tied to releases; it
  is a ledger item.

## Consequences

- `scripts/release.test.mjs` covers the tag check, reading the stamped version, finding the app
  partition (on the core's real `default_8MB` table and on synthetic ones), the DIO check, the
  checksums and the notices; `scripts/test_firmware_version.py` covers the version from git and
  the header, including a build without git, named by the variable or stopped. A copy of the
  source without `.git` stopped at the hook with the message and no traceback, and built
  `v0.1.0` with the variable set.
- The whole path ran in a throwaway clone tagged `v0.0.99`: `release.mjs` first refused the
  version for its missing changelog section, then packaged both boards. Each board, its flash
  erased, took its image with `esptool write-flash 0x0` alone and booted every part, its first line
  `[boot] Rattlebox v0.0.99 on M5StickC Plus2` and `[boot] Rattlebox v0.0.99 on Cardputer ADV`.
  Both bootloaders build DIO already (header byte 2 is `02`).
- A blind correctness review (`/wrap-up`) found the build step's failure hidden behind `tee` in a
  shell without pipefail, the traceback without git, and a release that ran no tests; each was
  fixed as above. A force-push to `main` makes `docs.yml`'s immutable check fail on the missing
  revision; that is kept, and said at the site.
- `NOTICES.md` carries ESP-IDF's `COPYRIGHT.rst`, which names each third-party component and its
  license but does not reproduce every component's own text (newlib's `COPYING.NEWLIB`, for one).
  Which components the firmware links, and so which texts it must carry, is a ledger audit.

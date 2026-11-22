# Security policy

## What counts

Rattlebox is a dice tray: it has no radio enabled, stores nothing of yours and talks to nothing but
a computer on its USB port. A security issue here is a defect that lets something reaching the
board, over USB serial or through a released firmware image, do what it should not: for example, a
serial command that corrupts memory or runs code, or a release image that does not match the
source it names.

A wrong roll or an unfair die is a bug, not a security issue: open a normal issue for it.

## Supported versions

Only the latest release, and `main`, get fixes.

| Version | Supported |
|---|---|
| Latest release, and `main` | Yes |
| Older releases | No |

## Reporting one

Report it privately, not in a public issue:

1. **Preferred:** GitHub's private vulnerability reporting, on this repository's **Security** tab,
   **Report a vulnerability**.
2. Otherwise, contact the maintainer, [@Isonimus](https://github.com/Isonimus).

Include the firmware version (the board's first boot line over USB serial,
`[boot] Rattlebox <version> on <board>`), the board, the steps to reproduce and the impact. There
is no bug bounty; reports are credited if you would like.

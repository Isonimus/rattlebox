# Contributing to Rattlebox

Thanks for your interest. These notes make a contribution land smoothly.

## Setting up

You need [PlatformIO](https://platformio.org/install), Node.js 22 and Python 3.10 to 3.13. The
pinned Python tools are in `requirements.txt`. PlatformIO keeps its packages inside the project
(`.platformio/`), so the first build downloads the platform once.

```bash
pip install -r requirements.txt
npm test                         # host tests: scripts, Python and the native suite
pio run -e cardputer             # Cardputer ADV firmware
pio run -e m5stickc-plus2        # M5StickC Plus 2 firmware
```

To install the hook that checks the decision records on every commit:

```bash
ln -sf ../../.claude/hooks/pre-commit .git/hooks/pre-commit
```

## What a change needs

The engineering standards are in [`docs/quality-bar.md`](docs/quality-bar.md), and the rules
specific to this firmware are the standing invariants in [`CLAUDE.md`](CLAUDE.md), section 4. They
are written for AI-assisted work, but they apply to everyone. In short:

- **Both boards build.** Code outside `src/hal/boards/` is shared, so a change to it builds in both
  `cardputer` and `m5stickc-plus2`.
- **Logic has a test.** Host tests live in `test/` (Unity, `pio test -e native`). A regression test
  fails before the fix and passes after.
- **Fairness is measured.** A change to the physics, the throw, the spin, the shake detector, the
  tray's gravity or the shake fixtures runs `npm run test:fairness` (three dice at a time) as well as `npm test`.
- **What a person sees is checked on a board.** Rendering, physics and timing cannot be asserted in
  a unit test; the `npm run verify:*` checks drive a flashed board over USB serial and save screen
  captures. Say which board and which check you ran.
- **The README follows the device.** A change to what a person sees or does, or to how they build,
  flash or verify, updates `README.md` (with new captures when the screen changed) and
  `CHANGELOG.md`.

CI runs the host tests, the three-dice fairness test, both firmware builds with a warnings gate
and their release images merged, cppcheck, CodeQL and the decision-record checks on every pull
request, and a pull request needs it green.

## Contributing your shakes

If you have one of the boards, the most useful thing you can give the project is about 25 minutes
of your hands. The shake detector's threshold and the fairness test both rest on recorded shakes,
and every recording so far is one person's. Flash the 100 Hz IMU probe and let the script walk you
through nine gestures, three times each:

```bash
pio run -e cardputer-imu-probe-100hz -t upload        # or m5stickc-plus2-imu-probe-100hz
node scripts/imu-probe.mjs --env cardputer-imu-probe-100hz --port /dev/ttyACM0 --participant p2
```

The participant id is `p` and a number, never a name: your shakes become files in this public
repository. Ask in an issue for the next free number. The takes are saved under
`artifacts/imu-probe/<env>/p<N>/`; attach that folder to the issue, or export your shakes as
fixtures and open a pull request:

```bash
node scripts/imu-probe.mjs --env cardputer-imu-probe-100hz --analyse-only --participant p2 --export-fixtures
```

## Commits and pull requests

- **Conventional commits**: `feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `chore:`, with an
  optional scope. One logical change per commit, and a body that explains why.
- Fork, branch, and open a pull request against `main`, filling in the template.

## About `adr/` and `slices/`

They are the project's decision records, written with the
[Stele](https://www.npmjs.com/package/@isonimus/stele) method: each decision (ADR) and each feature
(slice) is written before its code and frozen once merged. Read them for the why behind the code.
You don't need to write one to contribute: describe the change in the pull request, and the
maintainer records any decision it makes.

## Reporting bugs and asking for features

Use the issue templates. A bug report needs the board and the firmware version, which the board
prints first at boot over USB serial (`[boot] Rattlebox <version> on <board>`). For a security
issue, follow [`SECURITY.md`](SECURITY.md) instead of opening a public issue.

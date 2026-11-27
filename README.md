# Rattlebox

<p align="center">
  <img src="docs/images/rattlebox-banner.webp" alt="Rattlebox: virtual dice tray with real physics" width="880">
</p>

<p align="center">
  <strong>A dice tray that fits in your hand: shake the device and the dice inside it roll</strong>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT"></a>
  <a href="https://github.com/Isonimus/rattlebox/releases/latest"><img src="https://img.shields.io/github/v/release/Isonimus/rattlebox" alt="Latest release"></a>
  <a href="https://github.com/Isonimus/rattlebox/actions/workflows/ci.yml"><img src="https://github.com/Isonimus/rattlebox/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://github.com/Isonimus/rattlebox/actions/workflows/codeql.yml"><img src="https://github.com/Isonimus/rattlebox/actions/workflows/codeql.yml/badge.svg" alt="CodeQL"></a>
  <img src="https://img.shields.io/badge/platform-ESP32%20%2F%20ESP32--S3-333?logo=espressif" alt="Platform: ESP32 / ESP32-S3">
  <img src="https://img.shields.io/badge/built%20with-PlatformIO-orange?logo=platformio" alt="Built with PlatformIO">
  <a href="https://www.npmjs.com/package/@isonimus/stele"><img src="https://img.shields.io/badge/method-Stele-6E44FF" alt="Built with Stele"></a>
</p>

<p align="center">
  <a href="#how-it-plays">How it plays</a> •
  <a href="#controls">Controls</a> •
  <a href="#supported-boards">Boards</a> •
  <a href="#installing-a-release">Installing</a> •
  <a href="#building-and-flashing">Building</a> •
  <a href="#verifying">Verifying</a> •
  <a href="#roadmap">Roadmap</a> •
  <a href="#contributing">Contributing</a> •
  <a href="#built-with-stele">Method</a>
</p>

Firmware for the M5Stack Cardputer ADV and the M5StickC Plus 2 in which 3D dice tumble inside
the device, driven by its accelerometer. Shake it and they clatter against the walls and each
other; tilt it and they slide; let them settle and the face each one lies on is its result.

<table>
  <tr>
    <td width="14%" align="center"><img src="docs/images/cardputer-three-d20.png" alt="Three D20 locked on the Cardputer ADV, results 18, 3 and 13 totalling 34" width="260"><br><sub><b>Three D20</b> on the Cardputer ADV</sub></td>
    <td width="14%" align="center"><img src="docs/images/cardputer-three-d6.png" alt="Three D6 locked on the Cardputer ADV, results 2, 4 and 4 totalling 10" width="260"><br><sub><b>Three D6</b> on the Cardputer ADV</sub></td>
    <td width="14%" align="center"><img src="docs/images/stickc-three-d8.png" alt="Three D8 locked on the M5StickC Plus 2, results 6, 5 and 5 totalling 16" width="260"><br><sub><b>Three D8</b> on the M5StickC Plus 2</sub></td>
    <td width="14%" align="center"><img src="docs/images/cardputer-three-d10.png" alt="Three D10 locked on the Cardputer ADV, results 10, 4 and 5 totalling 19" width="260"><br><sub><b>Three D10</b> on the Cardputer ADV</sub></td>
    <td width="14%" align="center"><img src="docs/images/cardputer-three-d12.png" alt="Three D12 locked on the Cardputer ADV, results 10, 4 and 2 totalling 16" width="260"><br><sub><b>Three D12</b> on the Cardputer ADV</sub></td>
    <td width="14%" align="center"><img src="docs/images/cardputer-three-d4.png" alt="Three D4 locked on the Cardputer ADV, each showing its result on all three visible faces, results 1, 2 and 3 totalling 6" width="260"><br><sub><b>Three D4</b> on the Cardputer ADV</sub></td>
    <td width="14%" align="center"><img src="docs/images/cardputer-d100.png" alt="One percentile roll locked on the Cardputer ADV, the tens die showing 90 and the units die 2, read together as 92" width="260"><br><sub><b>One d%</b> on the Cardputer ADV</sub></td>
  </tr>
</table>

> [!NOTE]
> v0.1 is the first release. Each board has a ready-made image on the
> [releases page](https://github.com/Isonimus/rattlebox/releases/latest); see
> [Installing a release](#installing-a-release).

## How it plays

- **Shake to roll.** A shake throws every die and spins it to a random orientation drawn from the
  chip's hardware random number generator, so the result does not depend on the face it showed
  before (ADR-0010). A shake is told from handling the device by how long the motion lasts, not
  how hard it is (ADR-0005): shake it briskly for about a second. A slow, rocking shake may not
  roll, and walking briskly with the device in your hand can start a roll, since its motion
  measures like a gentle shake (ADR-0024).
- **Tilt to slide.** The device's own motion is the tray's gravity, so tipping it slides the dice
  toward the low side.
- **Hold it at any angle.** People hold the device tilted toward their face, so the angle it is
  held still at counts as level. While dice are in flight, level follows your hand; once they
  lock, it changes only when you hold the device still again (ADR-0014).
- **Fair results.** A die's result locks the first time it comes to rest lying flat, and never
  changes until the next roll; a die propped on a wall or another die is nudged until it lies flat
  (ADR-0009). Each die type ships only after a fairness test over tens of thousands of simulated
  rolls through the same physics code passes (ADR-0002).
- **Readable results.** A strip above the tray shows the die type; once every die locks, it adds
  each die's result in that die's colour, and the total (ADR-0016).
- **Sound.** Every hit in the physics clacks: filtered noise through the Cardputer's speaker, a
  click on the StickC's buzzer. A shake's rattle is those same hits.

D4, D6, D8, D10, D12, D20 and d% are available, one to three dice of a type per roll. The device
boots with one D20. The D10 reads 1 to 10, so its 10 is the face a printed D10 marks 0. The D4 is
read like a printed one: it lands on a face, and the number at the raised corner, shown on all
three faces you can see, is its result.

The d% is two dice read as one. It shows as `D100`: one die carries 00 to 90 and the other 0 to 9,
and together they are 1 to 100, where 00 with 0 is 100. Because it is two dice, only one d% fits
the tray, so the count buttons stay at one while it is chosen and the strip reads like
`D100 70 3 = 73`.

## Controls

| What it does | Cardputer ADV | M5StickC Plus 2 |
|---|---|---|
| Roll | Shake the device | Shake the device |
| Next die type: D4, D6, D8, D10, D12, D20, D100, then D4 again | `Enter` | Button A (front) |
| Cycle one, two, three dice (the d% stays at one) | `Backspace` | Button B (side) |
| One die more / one fewer | `;` (up) / `.` (down) | |

Changing the dice throws the new set at once.

## Supported boards

| Board | Chip | Display | Sound | Motion sensor |
|---|---|---|---|---|
| M5Stack Cardputer ADV | ESP32-S3 | 240 × 135 | ES8311 codec and speaker | BMI270 |
| M5StickC Plus 2 | ESP32-PICO-V3-02 | 240 × 135 | Buzzer | MPU6886 |

Both boards are built from one codebase; only `src/hal/boards/` differs (ADR-0001). The StickC is
the slower chip with the smaller heap, so it sets the performance budget. The original Cardputer
is not supported.

## Installing a release

Each [release](https://github.com/Isonimus/rattlebox/releases/latest) has one image per board,
built by CI from the tagged source, which flashes at address 0x0. With
[esptool](https://docs.espressif.com/projects/esptool/) (`pip install esptool`), the board on USB:

```bash
esptool write-flash 0x0 rattlebox-v0.1.1-cardputer-adv.bin     # Cardputer ADV
esptool write-flash 0x0 rattlebox-v0.1.1-m5stickc-plus2.bin    # M5StickC Plus 2
```

The same image works in [M5Burner](https://docs.m5stack.com/en/download): add it as a *User
Custom* firmware for your board and burn it. Releases after v0.1.0 also carry
`rattlebox-<version>-<board>-app.bin`, the app alone, for an installer that writes it over the air
into its own partition layout, such as M5Launcher; don't flash that one at 0x0, it has no
bootloader (ADR-0025).

`SHA256SUMS.txt` checks the download (`sha256sum -c SHA256SUMS.txt`), and `NOTICES.md` carries the
licenses of the code compiled into the firmware. The first line the board prints over USB serial,
at 115200 baud, names the version it runs: `[boot] Rattlebox v0.1.1 on <board>`.

## Building and flashing

You need [PlatformIO](https://platformio.org/install) and, for the checks, Node.js 22 and Python 3.10
to 3.13 (`pip install -r requirements.txt` installs the pinned tools). The build stamps the firmware
with its version from git; a build from a downloaded source archive, which has no git history,
names it instead, as in `RATTLEBOX_VERSION=v0.1.1 pio run -e cardputer`.

```bash
pio run -e cardputer -t upload          # Cardputer ADV
pio run -e m5stickc-plus2 -t upload     # M5StickC Plus 2
```

The firmware logs its version, its boot and every roll over USB serial at 115200 baud. It also takes one-byte
commands there, which the device checks use: `R` rolls again, `T` switches the die type, `C`
cycles the count, and `S` sends a screen capture.

## Verifying

| Command | What it checks |
|---|---|
| `npm test` | Host unit tests, including the fairness test one die at a time, and the scripts' own tests |
| `npm run test:fairness` | The fairness test three dice at a time, run before a change to the physics or its input merges |
| `npm run lint` | The decision records: their structure, citations, and that committed ones only gained lines |
| `npm run verify:roll -- --env <env> --port <port>` | On a flashed board: boots, rolls 40 times three of each of the D4, D6, D8, D10 and D12 and 60 times three D20, checks every roll locks and the results pass chi-square, and saves screen captures |

`verify:skeleton`, `verify:shake` and `verify:sound` are the other device checks; each names its
usage at the top of its script in `scripts/`.

CI runs everything that needs no board on each push to `main` and each pull request: `npm test`,
the three-dice fairness test, both boards' firmware with no compiler warnings allowed in `src/`,
cppcheck, CodeQL, and the decision-record checks. Pushing a `v*` tag builds and publishes a release.

## Roadmap

v0.1 is the dice tray described above. v0.2 adds the D8, the D10, the D12, the D4 and percentile
dice, then dice notation typed on the Cardputer (`3d6+2`, `2d20kh1`), presets, roll history and a
hidden roll. The
plan, and what is deliberately not planned, is in [`docs/roadmap.md`](docs/roadmap.md).

## Contributing

Bug reports, ideas and pull requests are welcome: [`CONTRIBUTING.md`](CONTRIBUTING.md) says what a
change needs, and [`CHANGELOG.md`](CHANGELOG.md) what each release changed. Report a security issue
privately, as [`SECURITY.md`](SECURITY.md) describes. Everyone taking part follows the
[code of conduct](CODE_OF_CONDUCT.md).

## Built with Stele

This repo is developed with **[Stele](https://www.npmjs.com/package/@isonimus/stele)**, a
documentation-as-decisions method. Every durable decision is written as an immutable **ADR**
([`adr/`](adr/)) and every feature as a **slice** ([`slices/`](slices/)) before the code, shipped
in the same commit and frozen once merged, so the record of why is never rewritten, only
superseded. Open work lives in a single [`LEDGER.md`](LEDGER.md); [`adr/INDEX.md`](adr/INDEX.md) is
generated; and a pre-commit hook, with CI as its backstop, runs the linter on every commit. The conventions live in
[`CLAUDE.md`](CLAUDE.md). If the layout looks unusual, that is why: the commit history is meant to
read as a trail of decisions.

## License

[MIT](LICENSE). Code from others compiled into the firmware, and what its licences ask of a binary
release, is listed in [`docs/third-party-notices.md`](docs/third-party-notices.md).

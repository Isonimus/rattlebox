---
id: '0023'
title: "Shake audit: other people's shakes and handling against the detector's threshold and the fairness test"
type: slice
status: accepted
date: 2026-11-22
supersedes: []
superseded_by: []
---

## Goal

Two ledger audits stand between v0.1's exit criteria and its release, and both rest on one
operator's hands:

- ADR-0005's shake detector starts a roll when more than 0.23 of the last second's samples are
  over 0.5 g off 1 g. The 0.07 margin on each side was measured on 3 takes per gesture from one
  operator on 2026-10-23; a brisker pick-up or a softer, shorter shake could cross it.
- ADR-0010's fairness test chains rolls on twelve shake takes from the same operator on the same
  day. A shake unlike them (slower, rotational, one-handed) is untested.

This slice records other people's shakes and handling on both boards, adds three shake kinds
the first recording did not have, re-measures the detector's margin over everyone's takes, and
reruns the fairness test and the fairness probe's shake sweep with the new takes as fixtures. Its
answer is an ADR: the threshold holds, with the margin measured over everyone, or it changes,
or a gesture is declared not a roll, each with the numbers it rests on. Both ledger items close
at merge.

## Definition of Done

- **Given** a participant id and a 100 Hz probe env
- **When** `node scripts/imu-probe.mjs --env <env>-100hz --port <port> --participant p2` runs
- **Then** it walks the participant through the participant protocol (`pickup`, `setdown`,
  `walk`, `bump` and five shake kinds, three takes each) and writes the takes under
  `artifacts/imu-probe/<env>/p2/`, picking up where an interrupted run stopped

- **Given** a participant id that is not `p` followed by digits (a name, say)
- **When** the probe parses its arguments
- **Then** it refuses it, since the takes become fixture files in a public repository

- **Given** an env directory holding the operator's takes and participants' subdirectories
- **When** the probe analyses with `--analyse-only`
- **Then** the separation table pools every take, each labelled with its participant, and the
  takes table shows each take's busy share at the capture rate

- **Given** a participant's takes at 100 Hz
- **When** the probe runs with `--analyse-only --participant p2 --export-fixtures`
- **Then** each shake take is written to `test/fixtures/shakes/<board>-p2-shake-<kind>-<n>.csv`
  as `t_us,ax,ay,az`, and a take at any other rate is refused rather than exported

- **Given** the fixtures directory
- **When** the fairness test loads its shakes
- **Then** it loads every CSV there in name order, and an empty directory fails the test

- **Given** the fixtures before any participant's takes
- **When** `npm test` and `npm run test:fairness` run
- **Then** both pass as before, because the directory's name order is the old list's order

- **Given** participants' shake fixtures
- **When** `node scripts/fairness-probe.mjs` runs
- **Then** the original twelve takes keep their seeds, and each participant take is replayed
  with the product's kick, cut at detection, D6 and D20, pooled per shake kind

- **Given** takes from at least two people besides the operator, on both boards
- **When** the probe and the fairness runs are done
- **Then** an ADR records the busy-share margin over everyone's takes and the fairness results
  on the enlarged fixtures, and the threshold, the fixtures and the ledger follow it

## Design

**Reused, not rebuilt.** The IMU probe (slice 0004) already records, frames and analyses takes,
and its 100 Hz envs (`*-imu-probe-100hz`) are the product's rate, which is the rate the fixtures
and the threshold are at. This slice gives it participants rather than writing a second tool.
`capture_imu.py`, the CSV format, `separation()` and the busy share are used unchanged.

**Participants.**

- `--participant pN` writes to a subdirectory of the env's artifacts, so the operator's 2026-10-23
  takes stay where ADR-0005's numbers came from.
- The id must match `p<digits>`: a name typed there would end up in a public fixture's filename,
  and a pattern is checked where a promise to be careful is not.
- With a participant and no `--gestures`, the protocol is the participant set. It holds the four
  handling gestures that came nearest the threshold at 100 Hz (the ones slice 0004 confirmed at
  the real rate) and five shakes: the two recorded before, plus `shake-slow` (a slow, rocking
  roll), `shake-twist` (the wrist turned back and forth, the rotational shake the ledger names)
  and `shake-one-hand` (one hand, the other not steadying it). `rest`, `hold` and `tilt` are left
  out: their busy shares were near zero, far from the threshold.
- 54 takes per person (9 gestures, 3 takes, 2 boards). The StickC sends a take in about 7 s, so
  that comes to roughly 25 minutes a person.

**Analysis pools everyone.** `--analyse-only` loads the env directory and each participant
subdirectory. A take's label carries its participant (`p2/shake-slow-1`), so the worst shake and
the worst handling name whose they were. The takes table gains the busy share at the capture
rate, so a take near the threshold is visible and not only the worst of each kind.

**Fixtures.** `--export-fixtures` writes a participant's shake takes into `test/fixtures/shakes/`
trimmed to `t_us,ax,ay,az`, the columns the replay reads. The trim the existing twelve went
through was done by hand; doing it in the probe keeps the next one identical. It refuses a take
not at 100 Hz, since the device sees the IMU at 100 Hz (ADR-0005).

**The fairness test reads the directory.** Its twelve paths are a hard-coded list today; with three
participants there would be 102. It lists every `.csv` in name order with `std::filesystem`, and
fails on an empty listing. The list's present order is the name order, so the seeded results do
not move until a fixture is added. Considered and not taken: keeping the list and appending each
participant's files. It catches a deleted fixture, but git review does that already, and a list a
hundred lines long that must match a directory is the kind of second copy that drifts.

**The fairness probe keeps its seeds.** Each replay's seed is its take's place in `ALL_TAKES`, and
ADR-0010's numbers came from that order. The original twelve stay listed explicitly, in their order,
and the participants' takes are appended after them from the directory. The kick comparison
(`none`, `throw`, `reorient`, the spin lengths) stays on the original gentle takes: it chose the
kick, and is not repeated. The audit adds one replay group, the product's kick at cut 0 on every
participant take, D6 and D20, pooled per kind, so a shake kind the spin does not randomize shows
as a pool with too many repeats. The chains run on every take, as the fairness test does. The
gesture pattern in a fixture's name widens to allow a hyphen (`one-hand`).

**What the answer can be.**

- **No new take crosses the threshold:** the ADR records the margin measured over everyone.
- **A shake falls below 0.23 or a handling gesture rises above it:**
  - if a threshold still separates every take, the ADR moves it to the new midpoint;
  - if none does, the ADR says which gesture is not a roll, and the README says how to shake.
- **The fairness test or the probe fails on the new fixtures:** that is a fairness bug in the spin
  or the physics. It is fixed before the release, not waived.

## Verification

- **Node (`scripts/imu-probe.test.mjs`)**:
  - the participant pattern, accepted and refused;
  - the participant protocol's contents;
  - labels carrying the participant;
  - the busy share per take;
  - the fixture trim, and refusal of a take not at 100 Hz.
- **Node (`scripts/fairness-probe.test.mjs`)**: the original twelve keep their seeds with
  participant files present; participant takes get product-kick replays pooled per kind; a hyphened
  kind parses.
- **Native (`test/test_fairness`)**: the shakes come from the directory listing. Before any new
  fixture, `npm test` and `npm run test:fairness` pass unchanged, which shows the listing kept the
  order. An empty listing fails the test (its own test case on a temporary directory).
- **Device**: `scripts/imu-probe.mjs` with participants on both boards. Its summaries are the
  evidence the ADR cites. As a probe it is exempt from `package.json` wiring (R11), and so is
  `scripts/fairness-probe.mjs`.
- **Fairness**: `npm test`, `npm run test:fairness` and `node scripts/fairness-probe.mjs` on the
  enlarged fixtures.

## As built

Shipped on 2026-11-22. Three people recorded on both boards: the operator again (`p1`) and two
others (`p2`, `p3`), 162 takes. Their answer is ADR-0024: 0.23 failed, the threshold is now 0.275,
and walking and slow rocking shakes are documented limits.

| Definition of Done | Evidence |
|---|---|
| `--participant` records the participant protocol under the env's artifacts, and resumes | 27 takes per person per board under `artifacts/imu-probe/<env>-100hz/p1..p3/` |
| A participant id that is not `p<digits>` is refused | `imu-probe.test.mjs`: names, capitals, `p`, `p2a`, `../p2` and an empty id refused |
| `--analyse-only` pools everyone, labelled, with each take's busy share | Before any participant, all four envs' re-analysis matched their saved separation and takes exactly; after, the summaries name `p2/shake-slow-1` and `p3/walk-1` |
| `--export-fixtures` writes `t_us,ax,ay,az` fixtures at 100 Hz only | 81 fixtures; the trim keeps the captured text, as the twelve before it were cut by hand |
| The fairness test loads every CSV in name order; an empty directory fails | `test_the_shakes_are_every_csv_in_the_directory_in_name_order` |
| Before new fixtures, both fairness runs pass unchanged | The name order equalled the old list's (`diff` of the two); `npm test` and `npm run test:fairness` passed. With participants, name order puts `cardputer-p1-...` before `cardputer-shake-...`, so the test and the probe, which keeps ADR-0010's order, now draw different takes from the same seed: each is reproducible, not comparable to the other |
| The probe keeps the original seeds and replays participants with the product's kick | `fairness-probe.test.mjs`; the probe passed in 361 s on 93 takes |
| An ADR records the margin over everyone and the fairness results | ADR-0024, ADR-0005 amended at its threshold, both ledger audits closed |

**Fairness on the enlarged fixtures.** The fairness test passed one die (`npm test`) and three
(`npm run test:fairness`, 94 s) on all 93 takes. The probe's product-kick pools, cut at detection,
showed this share of results on the previous face (chance 16.67% and 5.00%):

| Kind | D6 | D20 |
|---|---|---|
| gentle (24 takes) | 16.67%, p 1.000 | 4.97%, p 0.790 |
| hard (18) | 16.49%, p 0.443 | 4.84%, p 0.241 |
| one-hand (18) | 16.40%, p 0.240 | 4.75%, p 0.061 |
| twist (18) | 16.81%, p 0.524 | 5.13%, p 0.328 |
| slow (9) | 16.80%, p 0.678 | 4.78%, p 0.236 |

All twelve chained product runs passed: chi-square p 0.122 to 0.946, repeat p 0.462 to 0.963.

**Deviations from the design.**

- **The threshold changed.** The design expected the margin to hold or move. It failed both ways:
  other people's pick-ups and set-downs crossed it, and brisk walks crossed every threshold that
  kept gentle shakes. The operator chose 0.275 from the trade-off table in ADR-0024.
- **Only detected shakes became fixtures.** The Definition of Done said each shake take. A shake
  the device misses makes no roll and so tests no fairness, and the fairness test fails on any
  fixture never detected, so the export skips the nine slow takes at or under 0.275 and names
  them. The probe carries the threshold as `DETECTOR_BUSY_SHARE`. The fairness test detecting
  every fixture is what holds it equal to the C++ one.
- **The empty-directory case is tested as an empty listing**, which `shakes()` refuses with a
  Unity assertion. A test cannot assert that an assertion fires without failing itself.
- **A re-export replaces the participant's set on that board.** The blind review found that a take
  re-recorded under the threshold left its old fixture behind; the export now removes the
  participant's fixtures for the board before writing. The same review found ADR-0024 counting
  324 takes where there were 198, corrected before commit.
- **A fairness-probe test assumed two kinds of shake.** The pooled-replay failure test loaded every
  D6 product replay, which with five kinds failed five pools; it now loads the gentle pool it means.

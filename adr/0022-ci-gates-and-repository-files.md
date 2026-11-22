---
id: '0022'
title: "CI checks every push to main and every pull request, with the Adversary's repository files adapted"
type: architecture
status: accepted
date: 2026-11-22
supersedes: []
superseded_by: []
---

## Context

With the repository public (ADR-0021), contributions can arrive from people who never read
`CLAUDE.md`, and two of its standing invariants held only by review, each with a ledger line: no
CI built both boards, so invariant #1 rested on the author remembering, and the three-dice fairness
test ran only by hand before a physics change merged (invariant #2, ADR-0010). The Stele
pre-commit hook names `.github/workflows/docs.yml` as its backstop for clones without the hook, and
no such file existed.

The Adversary, by the same author, carries a set of repository files this one lacked: contributing
and security policies, a code of conduct, a changelog, issue and pull request templates,
Dependabot, CI with a build-warnings gate and cppcheck, and CodeQL.

Measured on 2026-11-22, before the gates were added: cppcheck 2.13 reported one finding in `src/`,
`acosf` reachable with -2 in `upFace` (`src/physics/die_result.cpp`) if a shape had no faces, from
the sentinel the face search started from; cold builds of both boards printed 192 warnings each,
all in LovyanGFX's headers and none in `src/`.

## Decision

- **`ci.yml`**, on every push to `main` and every pull request: `npm test`; `npm run
  test:fairness`; each board's firmware built cold, with `scripts/lint-build-warnings.mjs` failing
  on any warning in `src/` and the RAM and flash use in the run's summary; and cppcheck over `src/`
  at its default severity, on a pinned runner image. The warnings gate is scoped by path, not
  `-Werror`, because LovyanGFX's headers warn under the `-Wextra` our sources build with; it is
  adapted from the Adversary's (adversary:ADR-0083).
- **`docs.yml`**: the hook's three checkers (lint, index, immutable bodies), with the immutable check
  run head against base.
- **`codeql.yml`**: C/C++ analysis of both boards' builds, on pushes, pull requests and weekly, with
  results outside our own code dropped before upload (adversary:slice-0015).
- **`release.yml`**: ADR-0021.
- **Dependabot** for the workflows' actions and for `requirements.txt`, where the Python tools CI
  installs are pinned. The platform and LovyanGFX stay pinned in `platformio.ini`, which Dependabot
  cannot read; upgrading them takes a device run (ADR-0001).
- **Repository files**: `CONTRIBUTING.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md` (Contributor
  Covenant 2.1, as the Adversary's), `CHANGELOG.md`, `.editorconfig`, and issue and pull request
  templates that ask for the firmware version the board prints at boot. `CHANGELOG.md` is a live
  document kept by hand (stele:ADR-0010), and each release's section is its description.
- `upFace` seeds its search from the first face instead of a sentinel, which ends the cppcheck
  finding with no change in behaviour; `test_die_result` and the fairness test pass unchanged.

Left out, as the Adversary's own concerns: its disclaimer, dependency lock, raw-Serial ratchet and
dashboard checks. A web flasher on Pages is a ledger item (ADR-0021).

## Consequences

- Invariant #1 is verified by CI's firmware jobs, and invariant #2's three-dice half by its fairness
  job (`CLAUDE.md` section 4); both ledger lines close.
- Every pull request costs about two firmware builds, two CodeQL builds and the fairness test's
  run; the PlatformIO packages are cached by `platformio.ini` and `requirements.txt`.
- The on-device checks still need a board, so CI runs their parsing tests only (the ledger keeps
  that line, ADR-0003).

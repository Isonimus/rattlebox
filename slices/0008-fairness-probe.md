---
id: '0008'
title: "Fairness probe: the physics core, rest detection and the result lock, and a host chi-square test of D6 and D20 rolls through them"
type: slice
status: accepted
date: 2026-10-23
supersedes: []
superseded_by: []
---

## Goal

Answer the last M0 question in `docs/roadmap.md`: over many simulated rolls of D6 and D20, does
the chi-square test pass, with how many rolls and at what significance level? ADR-0002 fixed the
model (the result is the face the simulated die rests on, per-vertex contacts with the tray,
spheres between dice, a randomised launch, a lock at first rest with measured thresholds), but no
physics exists yet, so this slice builds the physics core the product will run and measures it on
the host. It delivers two ledger items: the host fairness test behind invariant #2, which this
probe becomes, and the regression test behind invariant #3 that a locked result never changes.
The measured thresholds, constants and test size go into a new ADR, which the roll slice builds
on.

## Definition of Done

- **Given** a clean checkout
- **When** the eight firmware envs and the new `native-fairness-probe` env build
- **Then** all nine build with no warnings from code under `src/`

- **Given** a D6 and a D20 built for a given edge length
- **When** the native tests inspect them
- **Then** each is a closed solid with every edge at that length, every face wound outward, numerals 1 to N once each with opposite faces summing to N + 1, and the D20 still draws as it did in slice 0006

- **Given** a die released on a level tray
- **When** the physics steps it
- **Then** a die placed flat at rest stays where it is, a dropped die never sinks below the floor or leaves the tray past a stated tolerance, its energy never rises without input, and two dice that meet push apart

- **Given** a die whose speeds stay below the rest thresholds for the hold time
- **When** it rests flat on a face
- **Then** its result locks to the face pointing up; pushing, tilting or striking it afterwards never changes the result; only a new roll clears it; and a die resting propped on a wall or another die (cocked) does not lock

- **Given** a seeded random source
- **When** launches are drawn from it
- **Then** the same seed gives the same launches, and the starting orientations are uniform (the face up at launch passes the same chi-square test)

- **Given** the probe program on the host
- **When** `node scripts/fairness-probe.mjs` runs it
- **Then** it fails on any error or a roll that never settles, and otherwise writes `artifacts/fairness-probe/summary.{json,md}` with, per die type and per one die or three: the tally of results, the chi-square statistic and p-value per seed, the cocked share, the time to lock, the rest-speed figures the thresholds rest on, and the runtime

- **Given** the probe's measured figures
- **When** the thresholds and test size are chosen
- **Then** an ADR records the time step, the contact constants, the rest thresholds and hold time, the cocked tolerance and its measured share, the roll counts and significance level, and the figures they rest on

- **Given** the fixed-seed fairness regression test with the ADR's settings
- **When** `pio test -e native` runs it
- **Then** D6 and D20, one die and three, each pass at α = 0.001 with their cocked share under the ADR's bound, and a planted bias (one D20 vertex moved off the regular solid) fails it

## Design

**Choices made before the design (2026-10-23, with the operator).**

- **Cocked dice do not lock.** A die locks only when it rests flat on a face, so the result
  always matches the picture. The probe counts cocked rolls and leaves them out of the tally;
  what the player sees for one is the roll slice's call. Because leaving rolls out could hide a
  bias, the regression test also bounds the cocked share.
- **Contacts are resolved by sequential impulses**: an impulse per contact with restitution and
  Coulomb friction, a fixed number of iterations, and positional correction. Penalty springs were
  rejected: stiff, they need a smaller step and jitter at rest, which fights the rest detector.
- **The test is sized to catch one face 1.25× too likely** with 99% power at α = 0.001. From the
  noncentral chi-square, that is 18,000 D20 results (critical value 43.82 at 19 degrees of
  freedom) and 3,400 D6 results (20.52 at 5). At 1.5× it would be 4,500 and 900. The seed is
  fixed, so the test cannot flake.
- **The test runs one die alone and three together.** v0.1 rolls one to three dice and sphere
  collisions between dice lie on the path to a result. Each die of a three-die roll counts toward
  the tally, so 18,000 D20 results take 6,000 three-die rolls.

**What the test proves, and what it does not.** Every launch starts from a uniformly random
orientation, so correct physics is fair by symmetry. The chi-square test therefore catches code
that introduces bias: a face read wrongly, a mesh that is not regular, a solver or tray that
favours faces, a cocked rule that drops some faces more than others. It does not show that a real
die thrown this way would be fair, and nothing here claims so.

**Units and constants.** The physics is in millimetres and seconds, with real dice sizes, so
restitution and friction can start from real-world values: a D20 of about 20 mm across has an
edge of about 10.5 mm, and ADR-0007's 24 px edge then sets the screen scale (about 2.3 px/mm),
which makes the tray about 105 × 59 mm. A D6 is a 16 mm cube. Gravity is 9810 mm/s² on a level
tray. All constants live in one header that the probe, the tests and later the firmware read, so
tuning a constant reruns the fairness test against the new value.

**The step.** A fixed 1/240 s step, four per 60 fps frame. Results depend on the step, so the host
and the device must use the same one. Its cost on the device is the roll slice's to measure;
ADR-0007 leaves about 15 ms of each StickC frame beside the drawing.

**Bodies.** Position and linear velocity; orientation as a unit quaternion (renormalised each
step, where a matrix would drift off orthogonal) and angular velocity. Every Platonic solid of
uniform density has an isotropic inertia tensor, so a die's inertia is one scalar per type
(cube m a²/6, icosahedron φ² m a²/10) and no tensor is rotated each step. The quaternion converts
to the existing `Rotation` for drawing.

**Tray and contacts.** Six planes: the floor, four walls and the glass as a ceiling, so a hard
throw stays inside. Each step, every die vertex past a plane is a contact (ADR-0002). Two dice
whose bounding spheres overlap are one sphere contact. The solver runs its iterations over all
contacts, then corrects positions by a fraction of the penetration, bounded so it adds no energy.

**Rest, cocked and the lock.** A die is at rest when its linear and angular speeds stay below
thresholds for a hold time. At rest, the face whose normal is nearest straight up is its
candidate; if that normal is within the cocked tolerance of vertical the die locks to it,
otherwise it is cocked. Once locked, the result is a field nothing but a new roll writes; the
die keeps moving physically (invariant #3). The thresholds, hold time and tolerance come from the
probe:

- the probe records, per roll, its linear and angular speeds and its up face each step;
- for a grid of candidate thresholds, it finds the longest stretch below them after which the up
  face still changed (a die pausing on an edge before tipping). The hold time must exceed the
  longest such stretch across all rolls, with margin;
- at rest it records how far the up face's normal is from vertical. Flat and propped dice should
  form two separate groups, and the tolerance goes in the gap.

**Launch.** A uniformly random orientation (Shoemake's method from three uniforms), a spin about a
random axis, a random throw direction and speed in a range, and random start positions that keep
three dice apart. The launch draws from a uniform-source interface: the host passes a seeded
generator, and the device will pass `esp_random()` with an entropy source on (ADR-0002). One
throw-speed range stands in for a typical shake; how shake energy maps onto it is the roll
slice's.

**A roll's end.** A roll ends when every die is locked or cocked, or at a simulated time limit.
A die that reaches the limit still moving is unsettled, which the probe and the test fail on:
it means the physics never brings a die to rest, not a result to drop.

**Code layout.**

- `src/geometry/die_shape.{h,cpp}`: one shape type for every die, holding vertices, faces as
  convex polygons of up to four corners (a D6 face is a square), outward normals and numerals,
  with `makeD6` and `makeD20`. It replaces `D20Mesh`: the physics needs both shapes now, so a
  common type is a present need, not speculation. A D12's pentagons (v0.2) will raise the corner
  limit then.
- `src/render/`: `viewDie` takes a `DieShape` and a px-per-mm scale, and a polygon face is drawn
  as a fan of triangles. No change to what the D20 looks like.
- `src/physics/`: the constants, quaternion, rigid body, tray planes, contact solver, world step,
  rest detection and lock, and launch. Pure C++ with no Arduino dependency (ADR-0002), so the
  native env compiles it. It sits outside `src/hal/boards/`, so invariant #1 applies and every
  env builds it.
- `src/probes/fairness/`: the simulated roll (launch, step until the end, read results) and the
  probe's `main()`. The regression test includes the simulated roll the way `test_imu_wire`
  includes the IMU probe's code, so the test and the probe run the same rolls.

**Existing code considered.** `Vec3` and `Rotation` (`src/geometry/`) are reused as they are; the
quaternion is new, because integrating a matrix drifts and slice 0006 needed none. `D20Mesh` is
generalised rather than copied for the D6. `summarizeDurations` (`src/probes/render/frame_stats`)
is not reused: it lives in another probe's folder and gives only timing percentiles, and the
fairness statistics are aggregated by the host script from per-roll lines instead. The probe's
host script follows `scripts/render-probe.mjs` (parse, check, summarise, write artifacts), but
runs a native program instead of a serial monitor, so it shares none of its process code. No
physics or random-number code exists in this repo or in Dragon Radar.

**Host side.** `native-fairness-probe` is a native env that builds `src/geometry/`,
`src/physics/` and `src/probes/fairness/` into a program. `scripts/fairness-probe.mjs` builds it,
runs it, parses its lines, computes each chi-square p-value, and writes the artifacts. It is a
probe, so it is exempt from `package.json` wiring (R11); the check it becomes is
`test/test_fairness`, which `npm test` already runs through `pio test -e native`.

**At merge.** Rows 2 and 3 of `CLAUDE.md` §4 move from `pending (LEDGER)` to the tests that now
enforce them.

## Verification

- **Native (`pio test -e native`)**:
  - `test_geometry` for both shapes (edges, winding, numerals);
  - `test_render` still passing on the shared shape type;
  - `test_physics` for a resting die staying put, no sinking or escape, energy never rising, and
    two dice pushing apart;
  - `test_rest_lock` for locking, cocked dice and invariant #3 (a locked result survives pushes,
    tilts and strikes; a new roll clears it);
  - `test_launch` for seeded repeatability and uniform orientations;
  - `test_fairness` as the fixed-seed regression for invariant #2, plus the planted-bias check.
- **Node (`scripts/fairness-probe.test.mjs`)**: parsing of the probe's lines, the chi-square
  p-value against known values, failure on an error or an unsettled roll, and the summary.
- **Probe**: `node scripts/fairness-probe.mjs`; `artifacts/fairness-probe/` is the evidence the
  ADR cites. As a probe it is exempt from `package.json` wiring (R11).
- **Builds**: the eight firmware envs and `native-fairness-probe` from clean, counting warnings
  from `src/`.

## As built

Shipped on 2026-10-23. The probe's answers are ADR-0009: the physics steps at 1/240 s with
warm-started speculative contacts, a result locks after 0.15 s below 5 mm/s and 0.5 rad/s lying
within 0.5° of flat, a die that rests cocked gets a random nudge, and the fairness test runs
18,000 D20 and 3,400 D6 results at α = 0.001. Every tally passed, the lowest p across twelve
being 0.032.

| Definition of Done | Evidence |
|---|---|
| Nine envs build with no warnings from `src/` | All nine from a clean build, 0 warnings from `src/`; the eight firmware envs compile `src/physics/` and `src/geometry/` for the ESP32 (invariant #1) |
| D6 and D20 are closed, outward, regular, with opposites summing to N + 1; the D20 draws as before | `test_geometry` checks both shapes by edge lengths, Euler's formula, winding, numerals, and inertia integrated from the faces; `test_render` passes unchanged on the shared type, plus a D6 square face and a scale test |
| A die rests, stays in the tray, gains no energy, and dice push apart | `test_physics`: D6 and D20 resting flat stay put; 40 throws of three of each stay within 0.015 mm of every plane and come to rest; the energy test (see deviations); two dice meeting part in order; a tilted tray slides a die to the wall |
| Locking, cocked dice and invariant #3 | `test_die_result`: lock after the hold, not before; motion past either threshold restarts it; a propped die is cocked, a lean inside the tolerance locks; a locked result survives every pose and motion fed to it and a strike through the real physics that turns the die onto other faces; only a new roll clears it |
| Seeded launches repeat, orientations are uniform | `test_launch`: same seed, same launches; starts inside the tray, apart and in range; the launch face passes chi-square over 20,000 draws; directions are centred; nudges hop upward at their size |
| The probe writes the tallies, statistics and rest figures | `node scripts/fairness-probe.mjs` passed in 18 s; `scripts/fairness-probe.test.mjs` covers parsing, p-values against tabulated points, the tilt gap, and failure on an unsettled roll, an unfair tally, a missing tally or trace and a wrong face count |
| An ADR records the step, constants, thresholds, tolerance, test size and figures | ADR-0009 |
| The fixed-seed test passes for D6 and D20, one and three dice, and a planted bias fails | `test_fairness`, 27 s; see deviations for the planted bias |

**Deviations from the design.**

- **The energy claim was too strong.** "Energy never rises" cannot hold for a stepped
  integrator: gravity's kinetic energy is added with the velocity before the contacts act and its
  potential with the velocity after, so a contact that turns a die round gains up to
  m·dt·|g·Δv| + ½·m·g²·dt² (0.6% of a three-D6 throw's energy at worst). The test allows exactly
  that per step; anything the solver or the correction adds beyond it fails, which caught the
  correction pump described below.
- **Cocked dice are nudged, not tallied apart.** The design left cocked rolls out of the tally,
  bounded by a share; the probe found 32% of three-D20 rolls ending with a die propped up. The
  operator chose a random nudge (ADR-0009), so the test instead requires every roll to settle.
- **The planted bias is a loaded die**, not a moved vertex. A D20 vertex moved 5% failed with a
  statistic near 18,000, but for an artificial reason: its top face no longer lay parallel to its
  bottom, so dice read as cocked. A die whose body sits 0.25 mm off its centre of mass is the bias
  a real die has, and it scored 216.
- **The throw came from data.** The design's guess of a throw across the floor was replaced by
  ADR-0005's captures integrated to speed (750 to 3500 mm/s in any direction), after the guess
  let a D6 end on its launch face 59% of the time.
- **The solver grew three mechanisms the design did not name**: warm starting, speculative
  contacts and a bounce threshold derived from the step (ADR-0009 has the measurements). The
  position correction parts dice before the planes, after parting them last pushed a die 1.8 mm
  through a wall.
- **`upFace` takes no "up" vector**: the result is the face toward the glass whatever the tilt, so
  the parameter was unneeded generality.
- **`stepsIn()` rounds step counts**: `1 / PHYSICS_STEP_S` is 239.9999 in float, and truncating it
  had cost a step per second in the probe, the roll's time limit and the tests.
- **Host tests build at -O2 with `-fno-ipa-modref`** (see below).

**Tests that did not bite.** Faults were planted to check the new tests, and each failed one:
dropping warm starting, correcting planes before parting dice, a 50 mm/s bounce threshold, no
speculative closing, the old fixed contact margin, friction pushing along the slip, re-reading a
locked result, motion not restarting the hold, cocked dice locking, no nudge (thousands of
unsettled rolls) and a non-uniform launch. Two did not: a D6 with its 3 and 4 swapped is still a
valid, fair D6 (its mirror image), so no test should catch it; and the first version of the
two-dice test expected the spheres to touch, while a speculative contact bounces up to a step's
travel early, so it was corrected to allow that.

**Adversarial review.** One correctness reviewer (Sonnet, blind to intent) returned eight
findings. Accepted and fixed, each with a test that failed first where one could: a relaunch into
a used `World` kept the last roll's warm-start impulses (`test_launch` now relaunches into a used
world and compares with a fresh one); two random draws shared one expression, whose operands C++
evaluates in no set order, so a seed could roll differently per compiler (no test can fail on one
compiler; every tally changed when fixed, and all still pass, which is why ADR-0009's table is
the second run); the probe let a NaN p-value pass and accepted a tally of any length; the
fairness test would hang rather than fail if dice stopped locking; and the bounce test's bound
was loose enough to pass a bounce (now fails with the threshold removed). Measured rather than
fixed: a locked die another die knocks over keeps its result, as invariant #3 requires; the probe
now counts it (0.15 to 0.26% of D20 results in three-die rolls) and ADR-0009 hands its display to
the roll slice. Already recorded: whether the firmware compiler miscompiles too (ledger).
Rejected: a division by zero when two dice's centres coincide, which cannot happen since they
start in separate strips and the correction parts them every step; the reason is at the site.

**Found while building.** Host GCC 13.3 at -O2 miscompiled the launch loop: the launch uniformity
test saw every orientation turn the same face up. It was correct at -O0, under clang, under ASan
and UBSan, and with `-fno-ipa-modref`, and a GCC debug-counter bisection put it on one modref
points-to decision. The exploratory sweeps behind the constants had been built without the flag,
so the decisive ones were rerun through the probe env with it: the traces matched figure for
figure, the restitution sweep was rerun at 2000 rolls (ADR-0009's table), and the nudge sweep held
at 200 and 400 mm/s.

---
id: '0009'
title: "Dice physics in 1/240 s steps with warm-started speculative contacts; results lock after 0.15 s below 5 mm/s and 0.5 rad/s, cocked dice get a random nudge, and fairness is tested on 18,000 D20 and 3,400 D6 results at α = 0.001"
type: architecture
status: amended
date: 2026-10-23
supersedes: []
superseded_by: []
---

## Context

ADR-0002 settled the model: a die's result is the face its simulated body rests on, tray contacts
are tested per vertex, dice meet as spheres, launches are random, a result locks at first rest
with measured thresholds, and fairness is a host chi-square test whose size and significance a
probe would settle. Slice 0008 built the physics core and that probe
(`scripts/fairness-probe.mjs`, the `native-fairness-probe` env). Measured on 2026-10-23.

**The solver needed three fixes before a die could rest.** Each was found by a measurement and is
now guarded by a test in `test_physics`:

- With the impulses restarted from zero every step, 10 iterations left a D20 resting on a face
  drifting at 0.06 mm/s until it lost contact and rocked for good; 40 held it still. Warm starting
  (each persisting contact starts from last step's impulse) holds it still at 10.
- Pushing a vertex back out after it passed through the floor lifted the die without slowing it,
  which kept a landed D20 rocking at about 30 mm/s. Speculative contacts, which let a vertex close
  its gap this step but not pass it, remove the pass-through; each vertex's reach is its own
  approach speed times the step plus 1 mm.
- Stepped in time, a contact leaving at v comes back up to 2 g·dt faster, so bounces die out only
  above 2 g·dt / (1 − e). With a 50 mm/s threshold a landed D20 hopped 0.1 mm every other step
  indefinitely; the threshold is now derived from that bound with a margin of 1.5 (350 mm/s).

**The throw comes from the IMU probe.** Integrated from ADR-0005's captures on both boards, a
gentle shake moved the device at 755 to 1031 mm/s at its peak and a hard one at 2122 to 3557 mm/s,
along all three axes. A guessed throw of 150 to 400 mm/s across the floor had let a D6 end on its
launch face 59% of the time.

**Restitution was chosen by how much the physics decides.** Over 2000 single-die rolls each, the
share of results equal to the face up at launch:

| Tray restitution | D6 (chance 16.7%) | D20 (chance 5%) |
|---|---|---|
| 0.35 | 36.0% | 10.4% |
| 0.5 | 27.5% | 6.8% |
| 0.65 | 21.7% | 9.2% |

**Rest.** 2000 traces of each die, left alone for 6 s with nothing locking:

- A die at rest moved at most 0.00016 mm/s and 0.00002 rad/s.
- The longest pause below a pair of thresholds after which the die still tipped onto another face
  (the hold time must outlast it): at 1 mm/s and 0.1 rad/s, 0.025 s (D20) and 0.004 s (D6); at
  5 mm/s and 0.5 rad/s, 0.037 s and 0.033 s; at 20 mm/s and 2 rad/s, 0.138 s and 0.037 s.
- At rest, the up face's normal was at most 0.229° from vertical on a D20 lying flat and at least
  1.085° on one propped up (D6: 0.040° and 29.7°).

**Cocked dice are common in a tray this small.** At rest, 9.9% of single D20s and 1.4% of single
D6s were propped on a wall, and 32% of three-D20 rolls had at least one die propped on a wall or
another die. Frictionless walls still left 7.6% of D20s cocked, because floor friction alone holds
a die leaning on a wall; a floor friction of 0.2 brought it to 2.0% but left 9.8% of three-D20 rolls
cocked; chamfering the floor's edges at 45° made D6s rest on the slope (17%). Leaving cocked dice
unlocked would have meant "cocked, reroll" on a third of three-D20 rolls.

The operator asked whether the player could settle a cocked die by moving the device. Tilt is live
input, so they can, but a cocked die leans between two or three faces and tilting toward one chooses
it, which is why real tables reroll cocked dice; and a host test cannot model the player.

**A random nudge settles them.** A cocked die gets an upward hop in a random direction and a random
spin. Over 500 rolls of three D20s, a 400 mm/s nudge fired 0.18 times per die and at most 5 times
in a roll, the longest roll taking 1.7 s; 200 mm/s took up to 12 tries and 100 mm/s up to 33.

**Fairness.** One face 1.25× too likely is caught with 99% power at α = 0.001 by 18,000 D20 results
(critical value 43.82 at 19 degrees of freedom) and 3,400 D6 results (20.52 at 5), from the
noncentral chi-square; at 1.5× it would take 4,500 and 900. Tallied with three seeds each:

| Rolls | χ², seeds 1 / 2 / 3 | p, lowest | Nudges per die | Longest roll | Knocked over after locking |
|---|---|---|---|---|---|
| D20, one at a time, 18,000 | 28.2 / 23.7 / 31.8 | 0.033 | 0.11 | 1.9 s | 0 |
| D20, three at a time, 6,000 | 18.0 / 12.6 / 22.4 | 0.265 | 0.18 | 2.6 s | 0.15 to 0.26% |
| D6, one at a time, 3,400 | 2.5 / 5.8 / 1.6 | 0.322 | 0.02 | 1.2 s | 0 |
| D6, three at a time, 1,134 | 12.2 / 0.8 / 8.6 | 0.032 | 0.04 | 1.3 s | 0 |

A D20 loaded 0.25 mm off its centre (2.4% of its edge) scored 216, and 0.1 mm scored 50.6: the
test catches a load that small. "Knocked over" counts locked results whose die another die then
tipped onto another face before the roll ended: the result stands (invariant #3), so for those the
face on screen is not the result. Every launch starts from a uniformly random orientation, so correct
physics is fair by symmetry; what the test catches is code that introduces bias (a face misread, a
mesh not regular, a solver favouring faces, a load), not proof that a real die thrown so is fair.
Dice of a three-die roll can touch, so their results are not strictly independent; the tallies of
three at a time passed as the single ones did.

**A host compiler bug.** GCC 13.3 at -O2 miscompiled the launch loop through its modref pass: every
orientation drawn in a loop turned the same face to the glass. It was correct at -O0, under clang
at -O2, under ASan and UBSan (which reported nothing) and with `-fno-ipa-modref`; a debug-counter
bisection put it on a single modref points-to decision. The firmware's compiler is GCC 14.2, whose
modref summaries for these functions looked correct; it has not run them on a device.

## Decision

- **The physics steps at 1/240 s**, four steps per 60 fps frame (ADR-0007), on the host and the
  device alike, in millimetres, seconds and radians, with every constant in
  `src/physics/physics_config.h`.
- **Contacts are sequential impulses, 10 iterations, warm-started and speculative**, with a
  position correction that parts dice first and leaves the tray's planes last, so a die never ends
  more than the 0.01 mm slop through the tray. Bounces need an approach above
  1.5 × 2 g·dt / (1 − e).
- **The tray is 105 × 59 × 40 mm**, a D20 has a 10.5 mm edge and a D6 16 mm. The tray's restitution
  is 0.65 and friction 0.35; between dice, 0.5 and 0.2. These are feel values the roll slice may
  retune; the fairness test reruns against whatever is set.
- **A throw is 750 to 3500 mm/s in a uniformly random direction**, with a spin of 10 to 30 rad/s
  about a random axis, from a uniformly random orientation.
- **A result locks when the die stays below 5 mm/s and 0.5 rad/s for 0.15 s** (four times the
  longest measured false rest) **lying flat**, its up face within 0.5° of the glass's normal.
  *Amended 2026-11-23 by slice 0026: the hold is 0.17 s, four times the 0.0417 s a D8 paused
  before it tipped, and the fairness probe fails a die whose traced pause the hold does not
  outlast four times over. The D8 lay flat at most 0.066° and propped at least 1.874°, so 0.5°
  still holds.*
  *Corrected 2026-11-23 by ADR-0028: there is no one hold. Each die carries its own, measured
  from its own trace by this same four-times rule, because how long a die can balance before it
  tips is a fact about its shape and not about the product. The holds are D6 0.04 s, D20 0.16 s,
  D8 0.17 s and D10 0.21 s. The rest of this claim, the two speeds and lying flat within 0.5°,
  stands.*
  *Corrected 2026-11-24 by ADR-0031: 0.5 rad/s is not rest. A die creeping over an edge at under
  it was counted as still, and the holds had been growing to wait that out, which is why they kept
  ratcheting. The angular speed is 0.1 rad/s, and at it the holds are D6 0.01 s, D8 0.01 s,
  D10 0.07 s, D12 0.04 s and D20 0.04 s. The 5 mm/s, the lying flat within 0.5° and the four-times
  method all stand.*
- **A die at rest past 0.5° is cocked and is nudged at once**: 400 mm/s upward in a random
  direction and 5 rad/s about a random axis, from the same RNG as the launch, and its result starts
  over. Never aimed at a face, so no result is steered (ADR-0002); immediate, so the player cannot
  tilt it onto a chosen face first.
- **Invariant #2's test is `test/test_fairness`**: one die and three dice of each type, 18,000 D20
  and 3,400 D6 results, chi-square at α = 0.001, seed 1, every roll required to settle; and a
  loaded D20 that must fail. **Invariant #3's test is `test/test_die_result`.**
  *Amended 2026-10-24 by ADR-0010: the rolls are chained on recorded shakes, a second check
  counts results on the die's previous face, and three dice run with `npm run test:fairness`.*
- **Host tests build at -O2 with `-fno-ipa-modref`.** `test_launch`'s uniformity test is the guard
  against that miscompile coming back.

## Consequences

- `pio test -e native` takes about 27 s longer for the fairness test.
- The physics keeps its contacts in static RAM, about 7.5 KB, and each `World` carries 3.5 KB of
  warm-start memory; on the StickC that counts against the heap ADR-0007 already left tight.
- A speculative contact bounces up to one step's travel early, about 15 mm at the fastest throw;
  whether that shows on screen is the roll slice's to check.
- The device has not yet run the physics: its step cost on the StickC, the launch from
  `esp_random()` with an entropy source on, and whether GCC 14.2 reads results correctly are the
  roll slice's to show.
- About 1 D20 result in 500 in a three-die roll ends showing a face other than its locked result,
  knocked over by another die. The roll slice must make the locked result visible (the overlay
  `docs/roadmap.md` plans), so the player is not left reading the face.
- Retuning restitution, friction, the throw or the nudge reruns the fairness test; if a type fails,
  ADR-0002's rule holds: a new ADR decides the fix, and the type does not ship biased.

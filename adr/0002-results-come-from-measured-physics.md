---
id: '0002'
title: "Dice results: the face the simulated die rests on, with a randomized launch and fairness measured on the host"
type: architecture
status: amended
date: 2026-10-22
supersedes: []
superseded_by: []
---

## Context

Rattlebox shows the dice as physical objects inside the device. Tilting it slides them,
shaking it throws them, and they settle on their own. Shaking and tilting drive one engine:
a shake is an impulse and a tilt is the gravity vector.

The original concept proposed drawing each result from the RNG and then animating the die
into that result. With a live, tiltable tray, this has two problems:

- **Steering shows.** Rotating a die toward a predetermined face is invisible while it spins
  fast, but shows as a snap or drift when the die is already slowing, and the player's tilt
  input can push it there at any time.
- **Results can be changed after the roll.** If tilting a settled die can flip it, the
  player can turn a 2 into a 20. For a dice roller, that is a correctness defect.

The concept's physics used bounding spheres everywhere. A sphere has no faces, so a die
modelled as one never settles onto a face, and the reported result would not match the
picture.

Two options were weighed:

- **A. Honest physics.** The result is whatever face the simulated die rests on. Fairness
  then depends on the simulation and the launch, so it has to be measured.
- **B. Steered physics.** The RNG picks the result and the simulation is nudged toward it.
  Fair by construction, at the cost of the visible correction described above.

A was chosen. It keeps the picture and the result the same thing, and its fairness can be
measured: the simulation is plain C++ and runs on the host.

## Decision

- **A die's result is the face its simulated body comes to rest on.** There is no steering
  and no result assigned after the fact.
- **Collisions with the tray are tested per vertex** against the tray's planes, so a die tips
  over and settles onto a face. Die-to-die collisions use bounding spheres.
- **Every launch is randomized from the hardware RNG**: the initial orientation, angular
  velocity and offsets. `esp_random()` is a true RNG only while the radio is on or after
  `bootloader_random_enable()`, so the firmware turns an entropy source on explicitly. Shake
  energy scales how hard the dice are thrown, not which face comes up.
  *Amended 2026-10-24 by ADR-0010: a shake is not an impulse. The device's measured motion is
  the tray's gravity, and at each detected shake every die is thrown and spun to a uniformly
  random orientation.*
- **A result locks the first time its die comes to rest**, meaning its linear and angular
  speed stay below thresholds for a hold time. Moving the die afterwards, by tilting or a
  later collision, does not change the reported result. Only a new roll does. The
  thresholds come from measurement.
- **Fairness is measured on the host.** A probe runs simulated rolls of each die type
  through the same physics code, compiled natively, and runs a chi-square goodness-of-fit
  test against the uniform distribution. The probe slice settles the number of rolls and
  the significance level and records the measured figures. The check then stays as a
  native regression test.
- **If a die type fails the fairness test**, the fix is decided in a new ADR. Steering
  (option B) is the fallback and would supersede this ADR. A failing type is never shipped
  with the bias left in.

## Consequences

- The physics core has no Arduino dependencies, so the native env can compile it. Results
  are deterministic for a given launch and input on one platform, but not bit-identical
  between the host and the ESP32. That does not matter for a statistical test.
- The host test covers rolls left alone on a level tray. Tilting during a roll is the
  player's influence on a moving die, just as with a real tray, and the test does not
  measure it. It ends at the lock.
  *Amended 2026-10-24 by ADR-0010: the device's own motion now moves the dice and each roll
  starts where the last one ended, so the test chains rolls on recorded shakes and also checks
  how often a result repeats the die's previous face.*
- Each new die type is shippable only once it passes the fairness test (invariant #2 in
  `CLAUDE.md`).
- Sphere collisions between dice can let two dice overlap slightly on screen. That is
  accepted for v0.1, and a later ADR can replace the die-to-die model without touching how
  results are read.

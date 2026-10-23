# Roadmap

What Rattlebox is meant to become and the order it is built in. This is a live document:
it records the **current plan**, and changes when the plan does. It does not track progress.
Open work lives in [`LEDGER.md`](../LEDGER.md) and shipped work in `slices/`, so there are no
checkboxes here to drift out of sync with them.

## The product

**Rattlebox: a dice tray that fits in your hand.** Polyhedral dice live inside the device.
Tilt it and they slide, shake it and they tumble and clatter against the walls and each
other, hold it still and they settle. The face each die rests on is its result.

- **Boards**: M5Stack Cardputer ADV and M5StickC Plus 2 (ADR-0001), built in both envs from
  the first slice. The StickC is the slower SoC with the smaller heap, so it sets the
  performance budget.
- **Fairness**: results come from the simulated physics, and each die type's fairness is
  measured on the host before it ships (ADR-0002). A result locks when its die first comes to
  rest lying flat; a die that comes to rest propped on a wall or another die gets a random nudge
  until it lies flat (ADR-0009). Each detected shake throws every die and spins it to an
  orientation drawn from the hardware RNG, because a shake alone leaves a die near the face it
  showed before (ADR-0010).
- **One input**: the device's own motion, read by the accelerometer, is the tray's gravity, so a
  shake rattles the dice and a tilt slides them (ADR-0010).

## M0: probes

Each probe answers one question with a measured number, and that number goes into an ADR
before feature work relies on it.

| Probe | Question it answers |
|---|---|
| **IMU bring-up** | Does the BMI270 run from our own driver, and does the MPU6886 give raw samples? What accelerometer range, output rate and filtering separate a deliberate shake from handling the device? |
| **Render** | How long does a frame of three flat-shaded D20s with face numerals take on each board, including the 13 ms panel push? That sets the frame-rate target. |
| **Fairness** | Over many simulated host rolls of D6 and D20, does the chi-square test pass? How many rolls, at what significance level? |

The first slice comes before the probes: a firmware skeleton with the HAL copied in
(ADR-0001), both envs building, and display, input and raw IMU samples logged over serial.
Dragon Radar's serial screen capture (esp32-dragon-radar:ADR-0011) comes across with it, so
later slices can prove on-screen behaviour with captured frames.

## v0.1: the MVP

- **D6 and D20**, one to three dice of a single type per roll.
- **Shake to roll, tilt to slide**: one physics engine (ADR-0002), with a "level" reference
  captured from the holding angle at the start of each roll. People hold the device tilted
  toward their face, so without this the dice would always pile against the bottom edge.
- **Top-down camera** looking into the tray, so a die's result face points at the viewer.
- **Readable results**: numerals drawn in 2D at the projected centres of faces that point
  at the camera, then an overlay with each result and the total once all dice lock.
- **Sound**: a rattle while shaking and clacks on collisions. Neither board can vibrate, so
  audio is the only physical feedback.
- **Controls**: keyboard on the Cardputer, buttons A and B on the StickC.

Exit criteria: both envs build, the fairness test passes for D6 and D20, and a roll on each
board is captured from shake to locked result.

## v0.2

- **D4, D8, D10, D12 and d% (percentile)**, added as geometry data. D4 is read from its top
  vertex and D10 counts 0 to 9, so each brings its own result-reading rule, and each passes
  the fairness test before it ships (ADR-0002, ADR-0009). D12's pentagonal faces raise the
  shape's corner limit (`src/geometry/die_shape.h`).
- **Dice notation on the Cardputer**: `3d6+2`, `2d20kh1` (advantage), `4d6dl1`. Typing an
  expression is the one thing a keyboard device does better than a phone. The StickC
  cycles saved presets instead.
- **Presets and roll history**, stored in NVS.
- **Hidden roll**, for a game master.

## v0.3 and later

- **Dice cup** (StickC first): turn the device face down, shake, turn it over to see the result.
- **Shared tray over ESP-NOW**: one device rolls and nearby devices show the same result,
  with no router.
- **Themes**: dice and tray palettes.
- **Wake on shake** from the IMU's motion interrupt, with auto-sleep.

## Not planned

- Textured numerals on die faces. A D20 has a 24 px edge on these screens (ADR-0007), too
  small for legible texture.
- Full rigid-body physics between dice. Spheres are enough for the die-to-die case
  (ADR-0002).
- Other boards. The original Cardputer and the custom C3 board are out of scope (ADR-0001).

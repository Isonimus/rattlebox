# Changelog

What each release changed for the people who use it, newest first. It is kept by hand, in the
same commit as the change it describes. Each release's section is its GitHub release's description,
so `scripts/release.mjs` refuses to release a version with no section here (ADR-0021). The
decisions behind each entry are in [`adr/`](adr/) and [`slices/`](slices/).

## Unreleased

- **The D8.** An eight-sided die, rolled one to three at a time like the others and as wide as the
  D20. It passed the same fairness test over 5,100 simulated results. `Enter` or button A now steps
  through the types in order, D6, D8, D20 and back to D6, where it switched between D6 and D20.
- **The D10.** A ten-sided die, rolled one to three at a time like the others and as wide as the
  D20. It reads 1 to 10, so the face a printed D10 marks 0 shows as 10 and three of them total 3 to
  30. It passed the same fairness test over 7,000 simulated results. The type order is now D6, D8,
  D10, D20.
- **The D12.** A twelve-sided die, rolled one to three at a time like the others and as wide as
  the D20. It passed the same fairness test over 9,000 simulated results. The type order is now
  D6, D8, D10, D12, D20.
- **The D4.** A four-sided die, rolled one to three at a time like the others and as wide as the
  D20. It is read the way a printed D4 is: the die comes to rest on a face, and the number at the
  raised corner, repeated on all three faces you can see, is the result. It passed the same
  fairness test over 1,900 simulated results. The type order is now D4, D6, D8, D10, D12, D20, so
  the type button steps to the D4 first.
- **Percentile dice (d%).** A pair of ten-sided dice read together as 1 to 100: one shows 00 to 90
  and the other 0 to 9, and 00 with 0 is 100. It is the one type that is two dice rather than one,
  so only a single d% fits the tray and the count button holds at one while it is chosen. The band
  shows both dice and the percentile they make, as `D100 70 3 = 73`. It passed the fairness test
  over 163,000 simulated rolls of all 100 outcomes, rather than over each die's ten faces, because
  the two dice knock into each other and fair halves would not prove a fair pair. The type order is
  now D4, D6, D8, D10, D12, D20, D100.
- **Each die waits its own time before its result locks**, where every die used to wait the same
  0.15 s. How long a die can balance on an edge before it topples depends on its shape, so the
  wait is now measured per die: 0.01 s for a D6 and a D8, 0.04 s for a D12 and a D20, and 0.07 s
  for a D10. A new die no longer slows down the dice already there.
- **Every die settles noticeably faster.** A die creeping over an edge too slowly to see was
  being counted as already stopped, so each die had to wait out that creep before showing its
  result. The device now tells the difference, and the wait fell with it: a roll of three dice
  takes about 0.33 s for D6 against 0.37 s in v0.1.1, 0.37 s for D8, 0.47 s for D10 and 0.48 s
  for D20 against 0.67 s. A result still never changes once it is shown.
- **Type what to roll, on the Cardputer.** `3d6`, `3d6+2`, `1d20-1` and `d100` can be typed
  straight onto the strip above the tray, which becomes the field you type into, and `Enter`
  throws them. The digits, `d` and `-` are the keys they are printed on, and `+` is `Shift` and
  `=`. `Backspace` deletes a character and leaves the field when it empties, after which both keys
  mean what they always did. A roll with a modifier shows it: `3d6+2` reads `D6+2 4 2 6 = 14`, and
  a single die now shows its total too, because 17 and 19 are different numbers. Changing the dice
  with the buttons keeps the modifier you typed.
- **An expression the tray cannot throw is refused, not rolled as something else.** The tray holds
  three dice, so `4d6` is refused; a d% is two of them, so `2d100` is; and `1d7` is a die that does
  not exist. The strip says which, and what you typed is still there to correct. The modifier is a
  single digit, because that is what the strip has room for. Advantage and keep-or-drop
  expressions, `2d20kh1` and `4d6dl1`, are not in this release.
- The M5StickC Plus 2 has no keyboard, so it has no field: its two buttons are the whole of its
  controls, exactly as before.

## v0.1.1 (2026-11-23)

- **Installing with M5Launcher.** Each release also carries the app alone,
  `rattlebox-<version>-<board>-app.bin`, for over-the-air installers. The full image, which esptool
  and M5Burner flash, is unchanged.
- **The serial boot log names each canvas.** The two screen buffers report as `front canvas` and
  `back canvas`, where both said `canvas`, so a failure says which one ran out of memory.

## v0.1.0 (2026-11-22)

The first release: a dice tray for the M5Stack Cardputer ADV and the M5StickC Plus 2.

- **Shake to roll.** A shake throws every die and spins it to a random orientation from the chip's
  hardware random number generator, so a result does not depend on the face it showed before. A
  shake is told from handling the device by how long the motion lasts, with a threshold measured
  on three people's shakes and handling, so picking the device up or setting it down does not
  roll. Known limits: walking briskly with it can start a roll, and a slow, rocking shake may not.
- **Tilt to slide.** The device's own motion is the tray's gravity, and whatever angle you hold it
  still at counts as level.
- **Fair results.** A die's result locks the first time it comes to rest lying flat and never
  changes until the next roll; a die propped on a wall or another die is nudged until it lies flat.
  D6 and D20 each passed a fairness test over tens of thousands of simulated rolls through the
  same physics code.
- **D6 and D20**, one to three dice of a type per roll. A strip above the tray shows the die type,
  then each result in its die's colour and the total.
- **Sound.** Every hit clacks: through the Cardputer's speaker, or as a click on the StickC's buzzer.
- **Controls.** Enter switches D6 and D20 and Backspace cycles the count on the Cardputer, where
  `;` and `.` add or remove a die; buttons A and B do the same on the StickC.
- **Releases.** Each board has one image that flashes at 0x0, and the first boot line over USB
  serial names the version.

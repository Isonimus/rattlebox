# Changelog

What each release changed for the people who use it, newest first. It is kept by hand, in the
same commit as the change it describes. Each release's section is its GitHub release's description,
so `scripts/release.mjs` refuses to release a version with no section here (ADR-0021). The
decisions behind each entry are in [`adr/`](adr/) and [`slices/`](slices/).

## Unreleased

- **Installing with M5Launcher.** Each release also carries the app alone,
  `rattlebox-<version>-<board>-app.bin`, for over-the-air installers. The full image, which esptool
  and M5Burner flash, is unchanged.

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

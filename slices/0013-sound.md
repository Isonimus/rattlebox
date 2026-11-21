---
id: '0013'
title: "Sound: each hit in the physics clacks, as filtered noise on the Cardputer and a click on the StickC's buzzer"
type: slice
status: accepted
date: 2026-10-24
supersedes: []
superseded_by: []
---

## Goal

The roadmap's last v0.1 feature: a rattle while shaking and clacks on collisions, the only
physical feedback either board can give, since neither can vibrate. ADR-0001 kept an audio
interface from Dragon Radar but none was copied in; this slice brings the board audio over and
plays a clack for every hit the physics makes. The rattle is those same hits, with no second
sound source: a shake throws the dice into the walls and each other, so what the device plays
always matches what the screen shows.

It closes one ledger item:

- **[audit]** "With the second canvas, the StickC's largest free DMA block falls from 110,580 to
  about 45,000 bytes, measured with only the display up; the heap with audio, physics and the IMU
  running beside both canvases is unmeasured (ADR-0007)." The product logs its heap with every
  part up, and the lowest it fell over each roll; the figures land in As built.

## Definition of Done

- **Given** a roll that has just locked
- **When** the device is held still for 2 s more
- **Then** no clack plays

- **Given** dice at rest and the device held still
- **When** the device is shaken to roll
- **Then** the dice clack as they hit the walls and each other, a harder hit sounding louder,
  dense enough while shaking to be heard as a rattle, until they settle and lock

- **Given** the product rolling with sound on each board
- **When** the device is shaken to roll
- **Then** no frame runs late and no IMU sample is lost beyond the chips' measured clock error

- **Given** the StickC with the display, both canvases, the IMU, the physics and the buzzer up
- **When** it boots and rolls
- **Then** it logs its free heap and largest free DMA block at boot, and after each roll the lowest
  free heap since boot

- **Given** a board whose sound hardware does not start
- **When** it boots
- **Then** it logs `[boot] audio FAILED` with the reason and stops, as for any other part

- **Given** the fairness replay with an observer added for the probe
- **When** the fairness test runs one die at a time (`npm test`) and three at a time
  (`npm run test:fairness`)
- **Then** both pass, unchanged

## Design

**Hits from the solver's memory.** The physics keeps every contact's normal impulse for warm
starting (ADR-0009): for each die, vertex and tray plane, and for each pair of dice. `HitFinder`
(`src/sound/`) compares each step's impulses with the step before: a contact that pushes now and
did not then is a hit. A die's new tray contacts in one step are summed into one hit, so a face
landing on four vertices clacks once; a die pair is one hit. A contact that goes on pushing, as
under a resting die, never hits again. It reads `world.memory` and changes nothing in the physics.

**Measured on the fixtures.** `src/probes/clack/` (env `native-clack-probe`) replays the 12
recorded shakes (ADR-0010) 20 times each, on three dice, through the product's physics, kick and
`HitFinder`, then leaves the dice 2 s at rest. Impulses are in mass·mm/s; one die's weight held for
one step, a resting contact's push, is 40.9.

| Die | Phase | Seconds | Hits/s over 50 | Hits/s over 400 | Impulse p50 | p90 | p99 | 50 ms windows holding a hit over 50 |
|---|---|---|---|---|---|---|---|---|
| D6 | shaking | 1175.7 | 118.4 | 78.1 | 342.5 | 1621.9 | 4305.5 | 84.1% |
| D6 | settling | 60.2 | 9.4 | 1.5 | 17.9 | 293.6 | 787.3 | |
| D6 | at rest | 480.0 | 0 | 0 | | | | |
| D20 | shaking | 1175.7 | 95.8 | 69.8 | 517.2 | 2061.3 | 4991.9 | 87.0% |
| D20 | settling | 81.8 | 9.7 | 2.4 | 26.5 | 282.2 | 925.6 | |
| D20 | at rest | 480.0 | 0 | 0 | | | | |

At rest the D6 made 66 hits in 480 s, every one under 25, and the D20 none. So:

- **No clack below an impulse of 50**, a little over one resting push: it is twice the largest hit
  at rest, and it keeps 84 to 87% of the shake's 50 ms windows holding a clack, which is the
  rattle with no source of its own.
- **Loudness is proportional to the impulse, full at 5000**, about the shakes' 99th percentile,
  and clipped above it. A real knock's amplitude follows the momentum it carries, so a hit of 50
  plays 40 dB under one of 5000.

**Cardputer ADV: filtered noise over I2S.** Copied from Dragon Radar (esp32-dragon-radar:ADR-0004):
the ES8311 enable sequence, unchanged from M5Unified 0.2.27's `_speaker_enabled_cb_cardputer_adv`,
read on 2026-10-24, the I2S pins 41, 43 and 42, 16-bit stereo at 48 kHz, and a FreeRTOS task that
writes 5 ms blocks. Not copied: the tone sequencer and the square wave, which play melodies; a
clack is not a pitch. `ClackSynth` (`src/sound/`, pure) renders the clacks: white noise from a
32-bit xorshift, through a band-pass biquad, times one envelope that every clack raises by its
loudness and that decays exponentially. The filter is linear and every clack shares it, so clacks
that overlap need no voices of their own: their envelopes add, clipped at full scale. The centre
frequency, Q and decay are feel values, tuned by ear on the board. The task runs on core 0, away
from the frame loop on core 1, and takes the clacks the loop posts under a spinlock, once per
block, so a clack lands within 5 ms of its step.

**M5StickC Plus 2: a click on the buzzer.** The passive buzzer on GPIO 2 has no amplitude to set,
only an on/off drive. Each clack is a short LEDC square wave whose duty cycle, up to 50%, scales
with its loudness, stopped by a one-shot `esp_timer`, so the loop never waits on it. A clack
during a click restarts it, at the louder of the two duties. Frequency and length are feel values,
tuned by ear. LEDC needs no DMA buffer, so the buzzer takes nothing from the StickC's tight DMA
heap.

**The HAL.** `AudioHAL` (`src/hal/audio_hal.h`) is a `Startable` with one call, `clack(loudness)`,
and the longest a block of samples took to render, for the frame budget; the StickC renders no
samples and reports 0. Boot starts it with the other parts and halts if it fails. Dragon Radar's
`AudioHAL`, a melody player, is not copied: its five tune calls are nothing this product plays.

**Logging.** After a roll's `[frame]` line, `[sound] clacks=N render_max_us=N`, the clacks of the
roll and the longest block render, then `[heap] free=N min_free=N largest_dma=N`: the free heap,
its lowest since boot, and the largest block a DMA buffer could take. Boot prints the same heap line
once every part is up, since nothing allocates later. 2 s after a lock, `[sound] rest clacks=N
seconds=2.0` counts the clacks meanwhile. Held in the hand is still enough: the measured hand-held
tremor, 0.035 g (slice 0012), tilts the tray far less than the 19° at which a die slides on the
tray's friction of 0.35, so the dice at rest cannot move, and any clack there is a false one.

**Considered and reused.** The shake fixtures and the fairness replay, `simulateShakenRoll`, gained
an optional `ReplayObserver` told after each step, which only the probe passes; the fairness probe's
`StepObserver` watches a left-alone roll and does not know whether the device is shaking, so it was
not reused. The I2C bus, `cardputer_i2c`, already in the repo, carries the ES8311 writes.

## Verification

- `test/test_hit_finder` (`npm test`): a face landing is one hit of its whole impulse; a bouncing
  die hits each time it comes down; a resting die and dice pressed together make no hits; dice
  meeting head on are one hit; a contact that ends on a vertex or a die out of play hits again.
  Each was checked against a mutant of `HitFinder` that it kills.
- `test/test_clack_synth` (`npm test`): silence with no clack; a clack's burst decays to silence;
  a louder clack is louder; overlapping clacks add and clip at full; the loudness mapping's
  floor, scale and clip.
- `npm test` and `npm run test:fairness`: the fairness halves, unchanged.
- `scripts/sound-verify.mjs` (`npm run verify:sound`), wired into `package.json`, interactive: on a
  board running the product, it prompts for two shakes of each die type, each followed by
  holding it still until the board reports 2 s of rest. It fails on a boot failure, an error line, no `[boot] audio OK` or heap
  line, a shaken roll with no clack, a clack while the board lies at rest, a late frame, or IMU
  samples under the chip's rate. It writes `artifacts/sound-verify/<env>/summary.{json,md}` with the
  clacks, render times and heap figures. Whether it sounds like dice is for the person who shook it
  to say.
- Both envs build (invariant #1): the HAL gains an interface.

## As built

Shipped on 2026-11-21 as designed, with one feel value retuned on the StickC.

**Every Definition of Done scenario was met.**

- **Silence at rest.** On the host, no die at rest made a hit over 25 in 960 s of the clack probe;
  `test_hit_finder` holds a resting die and dice pressed together to no hits. On the device, every
  shaken roll of both boards played 0 clacks in the 2 s after its lock.
- **Clacks and a rattle.** The operator heard both boards play every shake and called the
  Cardputer's clacks right as first tuned: 3 kHz, Q 1.2, an 8 ms decay, at half of full scale.
- **No late frames, no lost samples.** `npm run verify:sound` passed on both boards:

  | Board | Rolls | Clacks a roll | Late frames | Longest frame work, ms | IMU rate, Hz | Longest block render, µs |
  |---|---|---|---|---|---|---|
  | Cardputer ADV | 4 | 44 to 58 | 0 | 13.2 | 98.8 to 99.1 | 173 |
  | M5StickC Plus 2 | 4 | 23 to 78 | 0 | 7.8 | 99.9 to 100.0 | none rendered |

  A render of 173 µs in each 5 ms block is 3.5% of the Cardputer's second core. The device counts a
  roll's clacks from its first detection, so the hits of the shake before it are not in these
  counts; the clack probe's rates are the measure of the rattle, these only show that it plays.
- **The StickC's heap with everything up.** At boot, 111,268 bytes free and a largest DMA block of
  47,092; the lowest free heap over every roll was 110,272. ADR-0007 measured about 45,000 bytes of
  DMA block with only the display up, so the buzzer, physics and IMU take nothing a DMA buffer
  could use. The ledger's audit item is closed by these figures.
- **A failed sound part stops boot.** Audio is a boot step like any other: `[boot] audio FAILED`
  with the reason, then the halt. Both boards logged `[boot] audio OK`; the failure path is the
  shared boot runner's, tested in `test_boot_steps`, and `sound-verify.test.mjs` fails a run on it.
- **Fairness unchanged.** `npm test` (169 C++ cases, 110 JavaScript, 32 Python) and
  `npm run test:fairness` (4 cases, 221 s) passed with the observer in the replay.

**Deviations and findings.**

- **The StickC's click was retuned by ear.** 4 kHz for 2 ms, eight cycles, was heard as "slightly
  high-pitched and chirpy": a pitch, not a knock. 2.5 kHz for 1 ms, two and a half cycles, was
  heard as much better, and the final verify ran on it.
- **The rest check is a hold, not a desk.** Written first as 3 s on a desk; built as 2 s held still
  after the lock, since hand tremor cannot slide a resting die (Design, Logging).
- **The synth's 16-bit clamp has no test.** Noise at full loudness peaked at 0.98 of the amplitude
  over 10^8 samples, under the filter's worst-case gain of 1.29, so no test input reaches it; it
  stays, because a float past int16's range has no defined conversion. A first test meant to cover
  it was dropped for not doing so.
- **Two hit-finder tests did not bite at first.** Hand-made mutants (a hit per vertex, no new-contact
  check on pairs, loops over only the dice and vertices in play) survived two of the first tests;
  they were replaced with tests that kill every mutant.
- **The ES8311 values carry a notice.** They are M5Unified's, so `docs/third-party-notices.md` gained
  its row and MIT notice.
- **A gravity bug was found, not fixed.** Held upright like a phone, the StickC's dice slid to the
  screen's top edge after a few shakes. Sound does not touch the gravity, so it predates this slice;
  it is in `LEDGER.md` to be measured before the level rule changes.

**The wrap-up's adversarial review** (one reviewer, blind to this slice) returned six findings:

- **Accepted and fixed, four.** `sound-verify` paired a roll with the next roll's rest line when a
  roll started within the 2 s of rest; it now fails the run instead. It checked the rest of shaken
  rolls only; it now fails on clacks at rest after any roll, launched ones included. Each change has a
  test that failed before it. On the board, the rest window counted the lock frame's four steps, whose
  hits were the roll's, and a roll that failed to lock opened a rest window over moving dice; rest
  steps are now counted per step after the lock's frame, and a failed roll has no rest.
- **Rejected, two.** A StickC click ending on the timer's task while a clack starts can drop that
  clack: one 1 ms click in a rattle of up to a hundred a second, inaudible, where a lock around LEDC
  calls from two tasks would cost more; the reason is at the site. The synth task's 4 KB stack was
  said to risk overflowing in `printf` on its failure path; low confidence and no measurement, and the
  path aborts either way.

The verify runs above came before these fixes, which change only how the rest is counted, not which
hits clack. After them, the StickC, flat and untouched on a desk, rolled 9 launched rolls with 0
clacks at rest each. An earlier run reported clacks at rest after launched rolls; its log showed the
die count changed and a shake was detected, so the board was being handled, and those were real hits.

---
id: '0037'
title: "Dice notation on the Cardputer"
type: slice
status: accepted
date: 2026-11-27
supersedes: []
superseded_by: []
---

## Goal

Type `3d6+2` on the Cardputer and the tray throws it. Dice notation is the one thing a keyboard
device does better than a phone (`docs/roadmap.md`, v0.2), and until now the Cardputer's whole
keyboard has been four keys doing what the StickC's two buttons do: step the type, cycle the
count, add or take a die.

ADR-0036 settled what the notation may say, because notation and this tray disagree about how
many dice exist: the grammar is `[N] d M [+K | -K]` over the characters `0` to `9`, `d`, `+` and
`-`, its bounds are the tray's bounds, a refused expression is refused out loud and never quietly
reduced, the band is the entry field, and `K` has one digit because the band has room for one.
Keep and drop (`2d20kh1`, `4d6dl1`) is not in this slice and is on `LEDGER.md`: `4d6dl1` needs a
fourth body in a tray whose frame budget is already measured as tight.

Nothing on `LEDGER.md` is closed by this slice. The two notation items it adds are the ones
ADR-0036 deferred, and both are deliberately still open.

## Definition of Done

- **Given** a Cardputer at boot, showing one D20
- **When** the player types `3d6+2` and presses Enter
- **Then** the tray throws three D6, the band shows `D6+2` with each die's result, and the total
  is the three dice plus 2

---

- **Given** the band showing an expression being typed
- **When** the player types `4d6` and presses Enter
- **Then** nothing is thrown and the band says the tray holds three

The band shows the reason in place of the expression, not beside it: `4d6 the tray holds 3` is 20
characters and the band has 19. The expression is still in the field, and the first Backspace or
character brings it back with the reason cleared, so the next scenario is how it is corrected.

---

- **Given** `1d100` typed and accepted, which is one die of two bodies
- **When** the player types `2d100` and presses Enter
- **Then** nothing is thrown and the band says a d% is one die, because two of them is four bodies

---

- **Given** an expression part way through being typed
- **When** the player presses Backspace until the field is empty
- **Then** the band goes back to showing the die type, and Backspace is SECONDARY again as it is
  today

---

- **Given** any expression the grammar refuses
- **When** the band draws the reason
- **Then** the reason fits inside the band's 240 px, laid out as `drawResultsBand` draws it

---

- **Given** the same firmware on the M5StickC Plus 2, which has two buttons and no keyboard
- **When** it runs
- **Then** no character can be typed, the entry mode is unreachable, and the controls do exactly
  what they did before this slice

## Design

### The grammar is parsed by a pure function

`src/roll/dice_notation.{h,cpp}`: `parseNotation(const char* text)` returns the `DiceChoice` the
expression asks for, or a refusal carrying the one reason the band will show. It takes a string
and returns a value, with no device type in its signature, so every rule in ADR-0036 is a host
test rather than something only a board can answer.

The type is looked up through the catalog's own `parseDieType`, by building the name the catalog
already spells (`d6` becomes `D6`, `d100` becomes `D100`). The notation therefore accepts exactly
the dice that exist, with no second list of faces to drift out of step with `die_catalog.cpp`,
which is the failure ADR-0034 and slice 0026 both exist to prevent. The count's bound comes from
`maxDiceOf(type)`, which already knows that a d% is two bodies and so fits once.

Each way an expression can fail has one fixed reason string. They are string literals, as
`numeralLabel`'s labels are, so a reason outlives the call that returned it; the band copies it
into its own segment anyway. A refusal that names a count (`the tray holds 3`) carries a
`static_assert` on `MAX_DICE`, so the prose and the constant cannot drift apart in silence.

### A key the grammar cannot use produces no character

The Cardputer's driver already decodes every key to its printed row and column
(`decodeTca8418Event`) and already prints each one to the serial port as `[kbd] down row R col C`.
So **no probe firmware is written**: the firmware that ships is the probe, and confirming a
position is pressing the key and reading the log.

The printed positions of the four bound keys were confirmed against hardware when they were
bound; the other 52 follow M5Stack's reference driver and have never been pressed. The table is
placed from three pieces of corroboration, each written at the table itself: the keycaps say where
a character is printed, `decodeTca8418Event` maps all 56 wired keys to distinct printed positions
(`test_tca8418_keymap` holds that), and the four confirmed positions are what that mapping gives
for them, across three of its four rows. **That is corroboration and not measurement**, so the
confirming press is on `LEDGER.md` rather than claimed here, and it needs a Cardputer on a port.

The keycaps also settle one thing the grammar cannot: there is no unshifted `+` on this keyboard,
only a shifted `=`. So the driver follows the Shift key, which arrives as a key event like any
other, and the table takes its state as an argument.

`InputHAL` gains a way to report a typed character, which the StickC implements as none: a
two-button board genuinely types nothing, so this is not an error to be loud about but the reason
no key there can open the entry. The serial port still can, which is how the device check drives
it on that board.

What a Cardputer key means moves out of the driver into `KeyboardInput`, which holds the bindings,
their gestures, the Shift state and **one ordered queue of characters and actions together**. The
driver keeps the I2C and nothing else. Two reasons:

- **Order.** Characters and actions are one stream. Written as two channels, a queue of characters
  and one latched action, the caller drains the characters first and a Backspace between two
  characters deletes the second instead of the first; an Enter takes the next expression's first
  character with it. Both need several key events in one drain, which is a frame that ran long,
  and a screen capture blocks for exactly that long. A single latch also dropped a second action
  in the same batch, which with Backspace deleting text is a keystroke the player made and never
  saw.
- **Testing.** The keyboard had no host test beyond its event decoder, because everything above it
  needed I2C. `KeyboardInput` takes decoded events and a clock, so the queue's order, the Shift
  state and the gestures are all host-tested.

The queue holds a whole drain's worth, sized from the controller's four-bit event counter rather
than its FIFO depth, plus one long press per bound key: nothing one frame can produce is ever
dropped, and a drop therefore means nobody is reading it, which the driver says out loud.

### The band is the entry field, and the entry is a state machine

`src/roll/expression_entry.{h,cpp}` holds the text, the length, whether the field is open and the
last refusal, and nothing else: no display, no HAL, no catalog. Typing a character opens the field
and appends; Backspace deletes and closes the field when it empties, handing Enter and Backspace
back to the controls they are bound to today; Enter parses, and on a refusal keeps the field open
with the reason, which is what "never quietly reduced" means in practice.

The reason is drawn in place of the expression, because the two together are wider than the band,
and the next keypress clears it and brings the expression back. So the refusal is what the player
sees until they answer it, and answering it is what makes it go away.

The field holds a few characters more than the longest valid expression (`1d100+9` is seven), so a
typo can be typed and then corrected rather than being refused at the keypress, where the player
cannot see what is wrong.

The band gains a one-segment message band, which `typeBand` is then written in terms of, since a
band of one plain segment is what it has always been. The segment capacity grows to hold the
longest thing a segment can now carry, which is a refusal reason rather than a result.

### The modifier rides with the choice

`DiceChoice` grows the modifier beside its type and count, because it is part of what the next
roll throws. The total is already handed to the band rather than summed there (ADR-0034, since a
d%'s two bodies are one die), so the modifier lands in `lockedBand`, the one place that already
computes a roll's total.

Stepping the type or the count with the buttons **keeps** the modifier. It would be a lie only if
it were invisible, so the band shows it on the type segment: `D6+2` before the roll and in front of
the results after it. That costs the three characters of headroom ADR-0036 measured, and is why
`K` has one digit.

### What was considered and not reused

- `parseDieType` and `maxDiceOf` are reused, as above: the catalog stays the only list of dice.
- `PressGesture` and the four key bindings are left alone. Enter and Backspace keep their actions
  and the entry mode reinterprets them, rather than the driver growing a mode of its own: the
  driver's job is to say which key moved, and what a key means in a given mode is the app's.
- `resultsBand`'s segment layout is reused rather than a second text path added for the expression,
  so there is one place that lays out the band and one place that measures whether it fits.
- `snprintf` is **not** used for the reasons or the labels. Slice 0035 spent 116 us a frame on
  per-face formatting and ADR-0036's band is drawn every frame too.

## Verification

- `test/test_dice_notation` (new, native): every type the catalog carries parsed from its
  notation; the count at 1, at `maxDiceOf` and one past it; `d100` accepted once and refused
  twice; an omitted count read as one; unknown faces, too many digits, a modifier past 9, and the
  malformed shapes (`d`, `3d`, `3d6+`, `+2`, `33d6`) each refused with their own reason; and
  **every reason string laid out and asserted to fit the band's 240 px**, which is the Definition
  of Done scenario about the reasons.
- `test/test_expression_entry` (new, native): typing opens the field; Backspace to empty closes
  it; the length cap refuses a further character without losing the text; a refused submit keeps
  the field open and carries the reason; an accepted submit yields the choice.
- `test/test_tca8418_keymap` (extended): every character the grammar accepts has exactly one key,
  no key yields two characters, and a key outside the grammar yields none.
- `test/test_keyboard_input` (new, native): the keyboard's whole decision with no I2C in it. A
  Backspace between two characters keeps its place, an Enter submits what came before it and not
  what came after, two taps in one batch are both kept, Shift changes what the next key types, and
  the queue counts what it cannot hold. The first two fail against the two-channel design this
  slice started with, which is what they are for.
- `test/test_results_band` (extended): the modifier on the type segment, and the total including
  it.
- `test/test_dice_choice` (extended): stepping the type and the count keeps the modifier.
- `test/test_serial_commands` (extended): the typed characters and Enter and Backspace over the
  serial port, which is what makes the entry path driveable without a human at the keyboard.
- `scripts/notation-verify.mjs` (new, wired into `package.json` as `verify:notation`): drives a
  board over the serial port, types `3d6+2` a character at a time, captures the screen and checks
  the band; then types `4d6` and checks that nothing was thrown and the reason is on screen. Its
  parsing half runs in CI (`scripts/notation-verify.test.mjs`), its device half needs a board on a
  port. This covers the Definition of Done scenarios a unit test cannot: what the band actually
  draws, and that a refusal throws nothing.
- The keyboard-to-character hop is the one step no host test can assert, and it is the one this
  slice does not close: the positions are corroborated, the confirming press is a ledger item, and
  `## As built` records which boards this ran on.
- `npm run build:envs`, both firmware envs, `npm test` and `npm run lint`, as every slice.

## As built

Dice notation ships as designed. `parseNotation` turns a string into a `DiceChoice` with a
modifier or into one of six fixed reasons; `ExpressionEntry` is the field, drawn in the band; the
Cardputer's keys type the grammar's characters, and the serial port types the same ones so a
script can drive it.

Every Definition of Done scenario was met, with one correction made before merge and one
scenario this slice does not close:

- **`3d6+2` throws three D6 and the band shows `D6+2` with a total including the 2.** Met.
  Confirmed on an M5StickC Plus 2 by `npm run verify:notation`, which typed it through the serial
  port, read the board's `accepted D6 x3 mod=+2` and watched the roll it threw.
- **`4d6` throws nothing and the band says the tray holds three.** Met, and the scenario's wording
  was corrected before merge: the band shows the reason *in place of* the expression, because
  `4d6 the tray holds 3` is 20 characters and the band has 19. The expression is still in the
  field and the next keypress brings it back.
- **`2d100` is refused as one die.** Met, with its own reason, since "the tray holds 3" is true of
  bodies and false of dice.
- **Backspace to empty closes the field and hands the key back.** Met.
- **Every refusal fits the band.** Met twice over: each reason carries a `static_assert` against
  `BAND_MESSAGE_MAX_CHARS`, so one too long fails the build, and `messageBand` aborts rather than
  draw a message it cannot, so one would fail a device run as a panic rather than be truncated.
- **The StickC behaves exactly as before.** Met for its keys, and *not* for its serial port, which
  can type the same characters: that is how the device check drives the entry on that board, and
  it is written at `getTypedCharacter` so nobody reads the comment as a guarantee.
- **Typing on a Cardputer's own keys.** Not done. No Cardputer was on a port, so the key positions
  are corroborated rather than pressed and the ordered input queue is host-tested only. Both are
  one `LEDGER.md` item.

The adversarial pass found a defect this slice had introduced, and fixing it is the largest part
of what shipped. Characters were queued while actions latched in a slot of their own, and the loop
drained every character before applying the action, so a Backspace between two characters deleted
the second instead of the first, and an Enter took the next expression's first character with it.
It needs several key events in one drain, which is a frame that ran long, and a screen capture
blocks for exactly that long. A single latch also dropped a second action in the same batch. The
fix moved what a key means out of the driver into `KeyboardInput`, one ordered queue of characters
and actions together, which gave the keyboard its first host tests above the event decoder:
`test_a_backspace_between_two_characters_keeps_its_place` and
`test_an_enter_in_the_same_batch_submits_what_came_before_it` were both run against the two-channel
design and fail there, printing `3dS` for `3Sd` and `3d61P` for `3d6P1`.

Four more findings from that pass were kept as they are, each with its reason written at the site
rather than here, so the next reader meets it where the edit would happen: the field wins over a
roll that locks behind it (nothing is lost, the band is rebuilt from the results every frame), a
hold does nothing while the field is open, the serial port can type on a board with no keyboard,
and a Shift release lost to an I2C failure leaves Shift held. The skeleton probe now drains the
characters it does not use, because a queue nobody reads reports dropped input.

What it cost, against the commit before this slice: Cardputer RAM 54,020 to 54,084 bytes and
flash 496,295 to 498,851; StickC RAM 53,496 to 53,520 and flash 451,707 to 453,699.

Checks run: `npm test` 508 cases (229 node, 279 PlatformIO), `npm run lint` clean over 37
documents, `npm run build:envs` 13 of 13, both firmware envs with no warnings in `src/`, and
`npm run verify:notation --env m5stickc-plus2` twice, before and after the input fix.

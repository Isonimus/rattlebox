---
id: '0036'
title: "An expression is bounded by the tray, not the notation"
type: architecture
status: accepted
date: 2026-11-27
supersedes: []
superseded_by: []
---

## Context

Dice notation is the one thing a keyboard device does better than a phone (`docs/roadmap.md`,
v0.2): type `3d6+2` instead of stepping a type with one button and a count with another. The
roadmap names three expressions, `3d6+2`, `2d20kh1` and `4d6dl1`, and that list is the problem,
because notation and this tray disagree about how many dice exist.

**The tray holds three bodies and cannot hold more.** ADR-0034 refused `MAX_DICE = 6` on measured
grounds: the Cardputer's worst frame with three D12s was 15,222 us of the 16,667 a frame has
(`artifacts/roll-verify/cardputer/summary.md`), six bodies take dice-to-dice contacts from 3 pairs
to 15, and `ContactMemory` would grow by 6,192 bytes of static RAM. `4d6dl1` asks for four bodies.
A fourth body is a smaller step than a sixth, but it is a step into a budget that is already
measured as tight, and no probe has been run at four.

So "type any expression" and "watch every die tumble" cannot both hold. The product is a tray in
which real bodies tumble and settle, and the face a die rests on is its result: that is what the
physics, the fairness work (ADR-0002, ADR-0009) and every slice so far are built on. Notation is
a way to say what to throw, not a licence to report numbers nothing on screen ever showed.

Two further constraints bound what the answer may be.

**The band is twenty characters wide.** It draws the built-in 6 by 8 font at size 2, so a
character is 12 px of the 240 px width (`src/render/band_drawing.cpp`, where "D20 20 20 20 = 60"
is recorded as 204 px). Anything the band has to say, including why an expression was refused,
says it in twenty characters or it does not fit.

Laid out as `drawResultsBand` actually draws it, with a 4 px margin, 6 px between segments and
3 px of padding each side of a result's colour chip, the worst band the product shows today,
`D20 20 20 20 = 60`, ends at **202 px** of the 240. That leaves 38 px, which is three characters
and a little: the whole headroom a new field has to fit into.

**No character can reach the application.** `InputHAL::getAction` returns one of seven abstract
actions (`src/hal/input_hal.h`), which is all a two-button StickC can produce. The Cardputer's
driver binds four keys by printed position and maps them to those actions
(`src/hal/boards/cardputer/cardputer_input.h`). There is no path for a digit, and the four bound
positions are the only ones ever confirmed against hardware: the rest of the printed layout
follows M5Stack's reference driver and has never been pressed.

## Decision

**An expression is accepted only if the tray can throw it.** The grammar's bounds are the tray's
bounds, and a roll never holds more than `MAX_DICE` bodies, as ADR-0034 already requires.

**The grammar that ships in v0.2 is `[N] d M [+K | -K]`**, over the characters `0` to `9`, `d`,
`+` and `-`:

- `N` is how many dice, 1 to `maxDiceOf(type)`, and may be left out, which means one.
- `M` is a die's faces, and names a type in the catalog: 4, 6, 8, 10, 12, 20 or 100. `d100` is the
  d%, which is two bodies, so only one of it fits.
- `K` is a constant added to or taken from the roll's total, 0 to 9.

`K` has one digit because the band has room for one. Laid out as above, `D20+9 20 20 20 = 69` ends
at 226 px of the 240 and `D20+99 20 20 20 = 159` ends at 250 px, overflowing by 10 px. A modifier
the band cannot show is a modifier the band would have to lie about, so the limit is the band's
and it is written here rather than discovered later by a player whose total does not add up. The
two ways to buy those 10 px were weighed and refused: dropping the total's `= ` makes the total
read as a fourth die in every band, and halving the segment gap leaves 2 px of margin and breaks
on the next thing the band has to say. Raising it is on `LEDGER.md`.

Keep and drop (`kh`, `kl`, `dh`, `dl`), several dice groups in one expression, and mixed types in
one roll are **not** in v0.2. Keep and drop is the one the roadmap names, and it is held back
because `4d6dl1` is the expression that does not fit the tray: shipping the selector while the
count it exists for is refused would ship a grammar whose point is unreachable.

**A refused expression is refused out loud, on screen, and is never truncated or quietly
reduced.** Each way an expression can fail has one reason written for the band's twenty
characters, and the expression stays on screen to be corrected rather than being thrown as
something else. Silently rolling `3d6` when the player typed `4d6` would make the band lie about
what was thrown, which is the failure ADR-0009 made the band exist to prevent.

**The band is the entry field.** While an expression is being typed the band shows it; on
acceptance it goes back to showing the type and the results. The band is where the results of
what you type will appear, so it is where what you type belongs, and the tray keeps every pixel
it has.

**A key the grammar cannot use produces no character.** The keyboard delivers only the characters
the grammar accepts. A key that cannot appear in any expression must not enter the field, because
the field would then hold an input that is unparseable by construction and whose only possible
outcome is a refusal the player cannot act on.

**The grammar is stated over characters, not over keys**, so that it does not depend on which
physical key produces which character. That mapping is a fact about one board's keycaps and its
controller's wiring, and only four of the Cardputer's 56 printed positions have ever been
confirmed against hardware. A table placed from the keycaps is corroboration and not measurement,
and this product does not let the two be confused (`docs/roadmap.md`, M0): the evidence for each
position is written at the table, and confirming it by pressing the key is an open item rather
than a claim. No probe is needed for it, because the driver already prints the printed row and
column of every key it reads.

## Consequences

- `DiceChoice` grows a constant modifier beside its type and count. The total is already handed to
  the band rather than summed there (ADR-0034, because a d%'s two bodies are one die), so the
  modifier lands in the one place that already computes a roll's total, and the band shows it.
- `InputHAL` grows a way to report a typed character, and the StickC reports none. That is not a
  failure to be loud about: a two-button board genuinely types nothing, and the mode the
  characters drive is therefore unreachable there, which is what the roadmap already says (the
  StickC cycles presets instead).
- The Cardputer's keys change meaning while an expression is being typed: Enter accepts it,
  Backspace deletes a character, and an empty field hands both back to the controls they are bound
  to today. This is a user-facing change to the controls and updates `README.md` in the same
  change (invariant 6).
- **When keep and drop does arrive, the band shows every result with the dropped ones dimmed**,
  not only the ones that counted. Rolling `4d6dl1` rather than typing a number is worth doing
  because you watch the drop happen, and a die left in the tray with a number the band never
  mentions reads as a bug. This is recorded here so the next session inherits the decision instead
  of making it again; the work is on `LEDGER.md`.
- Twenty characters is now a published limit on anything the band says, the refusal reasons
  included. A later band that needs more has to earn it from the tray's pixels or from the font
  size, and either is a new decision.

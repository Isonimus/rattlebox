#ifndef DICE_CHOICE_H
#define DICE_CHOICE_H

#include <stdint.h>
#include "../hal/input_hal.h"
#include "die_catalog.h"

/**
 * @brief What the next roll throws: one type, one to MAX_DICE dice (docs/roadmap.md, v0.1), and a
 * constant added to the total.
 *
 * The modifier is part of the choice and not of the expression that set it, because the buttons
 * can step the type and the count without touching it and the band shows it either way
 * (ADR-0036). Only a typed expression can set it, so it is zero on a board with no keyboard.
 */
/**
 * @brief The largest constant a choice's modifier may be, either way.
 *
 * One digit, because the band has room for one: laid out, `D20+9 20 20 20 = 69` ends at 226 px of
 * the 240 and a second digit overflows it by 10 px (ADR-0036).
 *
 * It lives with the field it bounds rather than with the notation that was the first thing to set
 * one. The band and the preset store both need the bound and neither parses an expression, so a
 * constant in `dice_notation.h` made them depend on the grammar for a property of the choice.
 */
constexpr int8_t MODIFIER_LIMIT = 9;

struct DiceChoice {
    DieType type;
    uint8_t count;
    /** @brief Added to the roll's total, -MODIFIER_LIMIT to MODIFIER_LIMIT (ADR-0036). */
    int8_t  modifier;
};

/** @brief One D20 and no modifier until NVS presets arrive in v0.2. */
constexpr DiceChoice BOOT_CHOICE{DieType::D20, 1, 0};

struct ChoiceChange {
    DiceChoice choice;
    /** @brief The choice differs from before: the caller launches the new dice. */
    bool       changed;
};

/**
 * @brief The choice after @p action (slice 0011). PRIMARY steps to the next type and from the last
 * back to the first (slice 0026); SECONDARY cycles the count 1 up to the type's
 * most and back to 1, so the StickC's two buttons reach every choice; the Cardputer's UP and DOWN
 * add or take one die, stopping at the ends. A type that throws more than one body per die holds
 * fewer dice, and stepping onto it brings the count down to what fits (ADR-0034). Anything else
 * leaves the choice as it was.
 *
 * The modifier is carried through untouched: no action can express one, and the band shows it, so
 * changing the die keeps the bonus the player typed rather than silently dropping it (ADR-0036).
 */
ChoiceChange applyAction(DiceChoice choice, InputAction action);

#endif // DICE_CHOICE_H

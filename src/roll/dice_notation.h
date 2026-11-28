#ifndef DICE_NOTATION_H
#define DICE_NOTATION_H

#include <stdint.h>

#include "../render/results_band.h"
#include "dice_choice.h"

/**
 * @brief Dice notation, as much of it as this tray can throw (ADR-0036).
 *
 * The grammar is `[N] d M [+K | -K]`: `N` dice, 1 to `maxDiceOf(type)` and 1 if left out, of the
 * die with `M` faces, plus or minus the constant `K`. Its bounds are the tray's bounds, because
 * the tray holds three bodies and no expression may ask for a die that will not be thrown.
 */

/**
 * @brief The most characters the entry field holds.
 *
 * The longest valid expression is `1d100+9`, seven characters. The field holds a few more so that
 * a typo can be typed and then corrected with Backspace, rather than being refused at the
 * keypress where the player cannot yet see what is wrong.
 */
constexpr uint8_t EXPRESSION_MAX_CHARS = 10;

// Why an expression was refused, in the band's own words. One fixed string each, so a reason
// outlives the call that returned it as `numeralLabel`'s labels do, and each is asserted to fit
// the band: a reason too long to draw is a refusal the player cannot read (ADR-0036).
constexpr char NOTATION_REASON_SHAPE[] = "try 3d6+2";
constexpr char NOTATION_REASON_NO_SUCH_DIE[] = "no die that size";
constexpr char NOTATION_REASON_NO_DICE[] = "roll at least 1 die";
constexpr char NOTATION_REASON_TOO_MANY_DICE[] = "the tray holds 3";
constexpr char NOTATION_REASON_TWO_BODIES[] = "a d% is one die";
constexpr char NOTATION_REASON_MODIFIER[] = "modifier up to 9";

static_assert(sizeof(NOTATION_REASON_SHAPE) - 1 <= BAND_MESSAGE_MAX_CHARS, "the band cannot draw it");
static_assert(sizeof(NOTATION_REASON_NO_SUCH_DIE) - 1 <= BAND_MESSAGE_MAX_CHARS, "the band cannot draw it");
static_assert(sizeof(NOTATION_REASON_NO_DICE) - 1 <= BAND_MESSAGE_MAX_CHARS, "the band cannot draw it");
static_assert(sizeof(NOTATION_REASON_TOO_MANY_DICE) - 1 <= BAND_MESSAGE_MAX_CHARS, "the band cannot draw it");
static_assert(sizeof(NOTATION_REASON_TWO_BODIES) - 1 <= BAND_MESSAGE_MAX_CHARS, "the band cannot draw it");
static_assert(sizeof(NOTATION_REASON_MODIFIER) - 1 <= BAND_MESSAGE_MAX_CHARS, "the band cannot draw it");

/** @brief What an expression asked for, or why it was refused. */
struct ParsedNotation {
    /** @brief What to throw. Read it only when @ref accepted; a refusal leaves its count at 0. */
    DiceChoice  choice;
    bool        accepted;
    /** @brief One of the reasons above, or "" when accepted. */
    const char* reason;
};

/**
 * @brief What @p text asks the tray to throw, or the one reason the band will give for refusing it.
 *
 * It takes a string and returns a value, with nothing of the device in its signature, so every
 * rule of the grammar is a host test rather than something only a board can answer. An expression
 * the tray cannot throw is refused and never reduced to one it can: rolling three dice for an
 * expression that asked for four would make the band lie about what was thrown (ADR-0036).
 */
ParsedNotation parseNotation(const char* text);

/**
 * @brief Whether @p character can appear in any expression.
 *
 * The entry field takes these and nothing else, so it can never hold an input that is unparseable
 * by construction and whose only outcome is a refusal the player cannot act on (ADR-0036).
 */
bool isNotationCharacter(char character);

#endif // DICE_NOTATION_H

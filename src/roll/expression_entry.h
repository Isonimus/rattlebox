#ifndef EXPRESSION_ENTRY_H
#define EXPRESSION_ENTRY_H

#include <stdint.h>

#include "../render/results_band.h"
#include "dice_notation.h"

/** @brief The character drawn after the expression, so an empty field still shows where it types. */
constexpr char ENTRY_CURSOR = '_';

/**
 * @brief An expression being typed into the band (ADR-0036).
 *
 * The band is the entry field, because it is where the results of what you type will appear and
 * the tray keeps every pixel it has. This holds the text and nothing else: no display, no HAL and
 * no catalog, so the whole of what typing does is a host test.
 */
struct ExpressionEntry {
    char        text[EXPRESSION_MAX_CHARS + 1];
    uint8_t     length;
    /** @brief The field is taking characters, so Enter and Backspace belong to it. */
    bool        open;
    /** @brief Why the last submission was refused, or "": it stays on screen until it is answered. */
    const char* reason;
};

/** @brief A closed, empty field, which is what the product boots into. */
ExpressionEntry emptyEntry();

/**
 * @brief Appends @p character, opening the field if it was closed.
 *
 * @return false, leaving the field as it was, for a character the grammar cannot use or once the
 * field is full. The field is never silently rewritten: a refused keypress leaves the text the
 * player can see unchanged, rather than dropping a character out of the middle of it.
 */
bool typeCharacter(ExpressionEntry& entry, char character);

/**
 * @brief Deletes the last character.
 *
 * @return whether the field is still open. Deleting the last character closes it, which hands
 * Enter and Backspace back to the controls they are bound to on a board with no keyboard.
 */
bool backspaceEntry(ExpressionEntry& entry);

/**
 * @brief Parses what has been typed.
 *
 * An accepted expression closes and clears the field: it has become the choice. A refused one
 * keeps the field open and holds the reason, so the player corrects what they typed instead of
 * being handed a roll they did not ask for (ADR-0036).
 */
ParsedNotation submitEntry(ExpressionEntry& entry);

/**
 * @brief What the band shows while the field is open: the refusal if there is one, else the text
 * and the cursor.
 */
ResultsBand entryBand(const ExpressionEntry& entry);

#endif // EXPRESSION_ENTRY_H

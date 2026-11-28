#include "expression_entry.h"

#include <stdlib.h>

namespace {

/** @brief The cursor is drawn after the text, so the field shows one more character than it holds. */
constexpr uint8_t ENTRY_BAND_MAX_CHARS = EXPRESSION_MAX_CHARS + 1;
static_assert(ENTRY_BAND_MAX_CHARS <= BAND_MESSAGE_MAX_CHARS, "the band cannot draw the field it types into");

void clear(ExpressionEntry& entry) {
    entry.text[0] = '\0';
    entry.length = 0;
    entry.open = false;
    entry.reason = "";
}

} // namespace

ExpressionEntry emptyEntry() {
    ExpressionEntry entry{};
    clear(entry);
    return entry;
}

bool typeCharacter(ExpressionEntry& entry, char character) {
    if (!isNotationCharacter(character)) return false;
    // A full field keeps what it holds. The longest valid expression is well inside it, so a field
    // this long is already a typo, and the player can see that the text stopped growing.
    if (entry.length >= EXPRESSION_MAX_CHARS) return false;
    entry.text[entry.length++] = character;
    entry.text[entry.length] = '\0';
    entry.open = true;
    // The player is answering the refusal, so it stops being the answer.
    entry.reason = "";
    return true;
}

bool backspaceEntry(ExpressionEntry& entry) {
    if (entry.length > 0) {
        entry.text[--entry.length] = '\0';
        entry.reason = "";
    }
    // An empty field is a closed field: there is nothing left to correct, and Backspace means
    // what it means everywhere else again.
    if (entry.length == 0) clear(entry);
    return entry.open;
}

ParsedNotation submitEntry(ExpressionEntry& entry) {
    const ParsedNotation parsed = parseNotation(entry.text);
    if (parsed.accepted) {
        clear(entry);
        return parsed;
    }
    entry.reason = parsed.reason;
    return parsed;
}

ResultsBand entryBand(const ExpressionEntry& entry) {
    // A band for a field nobody is typing into would cover the type or the results with a cursor.
    if (!entry.open) abort();
    if (entry.reason[0] != '\0') return messageBand(entry.reason);
    char field[ENTRY_BAND_MAX_CHARS + 1];
    uint8_t at = 0;
    while (at < entry.length) {
        field[at] = entry.text[at];
        at++;
    }
    field[at++] = ENTRY_CURSOR;
    field[at] = '\0';
    return messageBand(field);
}

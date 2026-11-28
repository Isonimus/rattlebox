#include "dice_notation.h"

#include <string.h>

namespace {

// A count of more than one digit cannot be a tray this size, so a second digit is a refusal and
// not a number to read the front of.
constexpr uint8_t COUNT_MAX_DIGITS = 1;
static_assert(MAX_DICE < 10, "a count needs more than one digit");
static_assert(MAX_DICE == 3, "NOTATION_REASON_TOO_MANY_DICE names the count");

// The d% is the most-faced die the catalog carries and the most the product will ever show
// (DIE_NAME_MAX_CHARS), so three digits spell every die that exists.
constexpr uint8_t FACES_MAX_DIGITS = 3;
static_assert(DIE_NAME_MAX_CHARS >= 1 + FACES_MAX_DIGITS, "a die name cannot hold the faces typed");

// One digit is the whole modifier range, so reading one digit is the range check (ADR-0036).
constexpr uint8_t MODIFIER_MAX_DIGITS = 1;
static_assert(MODIFIER_LIMIT == 9, "NOTATION_REASON_MODIFIER names the limit, and one digit is it");

constexpr uint8_t DECIMAL_BASE = 10;
constexpr uint8_t IMPLICIT_COUNT = 1;

bool isDigit(char character) { return character >= '0' && character <= '9'; }

struct Digits {
    unsigned value;
    uint8_t  count;
};

/** @brief Reads at most @p maxDigits digits from @p cursor on, leaving @p cursor after them. */
Digits readDigits(const char* text, uint8_t& cursor, uint8_t maxDigits) {
    Digits digits{0, 0};
    while (digits.count < maxDigits && isDigit(text[cursor])) {
        digits.value = digits.value * DECIMAL_BASE + static_cast<unsigned>(text[cursor] - '0');
        digits.count++;
        cursor++;
    }
    return digits;
}

/** @brief A refusal, whose choice is left at a count of 0: no roll throws none. */
ParsedNotation refuse(const char* reason) { return {DiceChoice{}, false, reason}; }

} // namespace

bool isNotationCharacter(char character) {
    return isDigit(character) || character == 'd' || character == 'D' || character == '+' || character == '-';
}

ParsedNotation parseNotation(const char* text) {
    uint8_t cursor = 0;

    const Digits count = readDigits(text, cursor, COUNT_MAX_DIGITS);
    // "33d6" asks for 33 dice. Reading the 3 and throwing three of them would answer a different
    // expression than the one typed, which is the one thing a refusal exists to prevent.
    if (isDigit(text[cursor])) return refuse(NOTATION_REASON_TOO_MANY_DICE);

    if (text[cursor] != 'd' && text[cursor] != 'D') return refuse(NOTATION_REASON_SHAPE);
    cursor++;

    const uint8_t facesStart = cursor;
    const Digits faces = readDigits(text, cursor, FACES_MAX_DIGITS);
    // "3d" is an expression still being typed; "3d1000" is a die, just not one that exists.
    if (faces.count == 0) return refuse(NOTATION_REASON_SHAPE);
    if (isDigit(text[cursor])) return refuse(NOTATION_REASON_NO_SUCH_DIE);

    // The type comes from the catalog's own names, so the notation accepts exactly the dice that
    // exist and there is no second list of faces to drift out of step with it (ADR-0034). The
    // digits are matched as typed rather than as the number they add up to: "d06" is not a name
    // the catalog carries, and reading it as a D6 would accept an expression by guessing at it.
    char name[DIE_NAME_MAX_CHARS + 1];
    name[0] = 'D';
    memcpy(&name[1], &text[facesStart], faces.count);
    name[1 + faces.count] = '\0';
    DieType type;
    if (!parseDieType(name, type)) return refuse(NOTATION_REASON_NO_SUCH_DIE);

    const uint8_t dice = count.count == 0 ? IMPLICIT_COUNT : static_cast<uint8_t>(count.value);
    if (dice == 0) return refuse(NOTATION_REASON_NO_DICE);
    if (dice > maxDiceOf(type)) {
        // A type that throws two bodies a die is held to one for a reason of its own, and saying
        // "the tray holds 3" of a d% would be true of bodies and false of dice (ADR-0034).
        return refuse(dieKind(type).bodiesPerRoll > 1 ? NOTATION_REASON_TWO_BODIES
                                                      : NOTATION_REASON_TOO_MANY_DICE);
    }

    int8_t modifier = 0;
    if (text[cursor] == '+' || text[cursor] == '-') {
        const bool negative = text[cursor] == '-';
        cursor++;
        const Digits amount = readDigits(text, cursor, MODIFIER_MAX_DIGITS);
        if (amount.count == 0) return refuse(NOTATION_REASON_SHAPE);
        if (isDigit(text[cursor])) return refuse(NOTATION_REASON_MODIFIER);
        const int signed_amount = static_cast<int>(amount.value);
        modifier = static_cast<int8_t>(negative ? -signed_amount : signed_amount);
    }

    // Anything left over was typed and means something to the player, so it is not ignored.
    if (text[cursor] != '\0') return refuse(NOTATION_REASON_SHAPE);

    return {DiceChoice{type, dice, modifier}, true, ""};
}

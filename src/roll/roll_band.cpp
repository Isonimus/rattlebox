#include "roll_band.h"

#include <stdio.h>
#include <stdlib.h>

#include "dice_notation.h"

namespace {

// "D100-9": the name, a sign and the modifier's one digit.
static_assert(MODIFIER_LIMIT < 10, "a modifier of two digits does not fit the label or the band");
constexpr uint8_t MODIFIER_SIGN_CHARS = 1;
constexpr uint8_t MODIFIER_DIGIT_CHARS = 1;
constexpr uint8_t CHOICE_LABEL_CAPACITY = DIE_NAME_MAX_CHARS + MODIFIER_SIGN_CHARS + MODIFIER_DIGIT_CHARS + 1;

/** @brief Writes "D6" or, with a modifier, "D6+2" into @p label. */
void writeChoiceLabel(DieType type, int8_t modifier, char* label, size_t capacity) {
    const char* name = dieTypeName(type);
    // No modifier is written at all, so every band that had none reads exactly as it did before.
    if (modifier == 0) {
        snprintf(label, capacity, "%s", name);
        return;
    }
    // The sign is always shown: "D6-2" and "D6+2" are different rolls and one of them is the
    // player's, so the band spells which (ADR-0036).
    snprintf(label, capacity, "%s%+d", name, static_cast<int>(modifier));
}

} // namespace

ResultsBand choiceBand(DieType type, int8_t modifier) {
    char label[CHOICE_LABEL_CAPACITY];
    writeChoiceLabel(type, modifier, label, sizeof label);
    return messageBand(label);
}

ResultsBand lockedBand(DieType type, int8_t modifier, const uint8_t* numerals, uint8_t bodyCount) {
    const DieKind& kind = dieKind(type);
    // A roll that ends part way through a die has no value to show: half a d% is not a result.
    if (bodyCount == 0 || bodyCount > MAX_DICE || bodyCount % kind.bodiesPerRoll != 0) abort();
    BandResult bodies[MAX_DICE];
    for (uint8_t body = 0; body < bodyCount; body++) {
        bodies[body].text = numeralLabel(bodyNumbering(kind, body), numerals[body]);
    }
    // The total adds dice, not bodies: a d%'s tens and units make one die worth 73 (ADR-0034).
    int total = modifier;
    for (uint8_t body = 0; body < bodyCount; body += kind.bodiesPerRoll) {
        total += static_cast<int>(dieValue(kind, &numerals[body]));
    }
    char label[CHOICE_LABEL_CAPACITY];
    writeChoiceLabel(type, modifier, label, sizeof label);
    // One plain die and no modifier is the one roll whose total is already on screen as its only
    // result, and that is the band slice 0017 shipped.
    const bool totalSaysMore = bodyCount > 1 || modifier != 0;
    return resultsBand(label, bodies, bodyCount, BandTotal{total, totalSaysMore});
}

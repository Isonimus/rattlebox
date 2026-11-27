#include "roll_band.h"

#include <stdlib.h>

ResultsBand lockedBand(DieType type, const uint8_t* numerals, uint8_t bodyCount) {
    const DieKind& kind = dieKind(type);
    // A roll that ends part way through a die has no value to show: half a d% is not a result.
    if (bodyCount == 0 || bodyCount > MAX_DICE || bodyCount % kind.bodiesPerRoll != 0) abort();
    BandResult bodies[MAX_DICE];
    for (uint8_t body = 0; body < bodyCount; body++) {
        numeralLabel(bodyNumbering(kind, body), numerals[body], bodies[body].text, sizeof bodies[body].text);
    }
    // The total adds dice, not bodies: a d%'s tens and units make one die worth 73 (ADR-0034).
    unsigned total = 0;
    for (uint8_t body = 0; body < bodyCount; body += kind.bodiesPerRoll) total += dieValue(kind, &numerals[body]);
    return resultsBand(kind.name, bodies, bodyCount, total);
}

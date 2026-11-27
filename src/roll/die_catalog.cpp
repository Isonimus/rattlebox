#include "die_catalog.h"

#include <stdlib.h>
#include <string.h>

#include "../physics/physics_config.h"

namespace {

/**
 * @brief The table, built on first use.
 *
 * Not a file-scope object: building the shapes runs code, so a file-scope table would be read as
 * zeroes by anything that asked for a die from its own static initialiser, in whichever order the
 * linker happened to put the two translation units.
 *
 * The cost of that is the guard the compiler puts on a function-local static, and building the
 * shapes inside the first frame that names a die rather than before `setup` returns. Both are
 * well under a frame: six bodies of a few hundred float operations, against 16,667 us.
 */
const DieKind* catalog() {
    // One body each, and the second numbering entry is never read: it is Plain rather than left
    // to chance so that a row misread past its body count reads as the old behaviour.
    constexpr Numbering ONE_PLAIN_BODY[MAX_BODIES_PER_DIE] = {Numbering::Plain, Numbering::Plain};
    static const DieKind KINDS[] = {
        {"D4", makeD4(D4_EDGE_MM), 1, {ONE_PLAIN_BODY[0], ONE_PLAIN_BODY[1]}},
        {"D6", makeD6(D6_EDGE_MM), 1, {ONE_PLAIN_BODY[0], ONE_PLAIN_BODY[1]}},
        {"D8", makeD8(D8_EDGE_MM), 1, {ONE_PLAIN_BODY[0], ONE_PLAIN_BODY[1]}},
        {"D10", makeD10(D10_CIRCUMRADIUS_MM), 1, {ONE_PLAIN_BODY[0], ONE_PLAIN_BODY[1]}},
        {"D12", makeD12(D12_EDGE_MM), 1, {ONE_PLAIN_BODY[0], ONE_PLAIN_BODY[1]}},
        {"D20", makeD20(D20_EDGE_MM), 1, {ONE_PLAIN_BODY[0], ONE_PLAIN_BODY[1]}},
        // The d%'s solid is the D10's, built a second time: a percentile die is a D10, so its own
        // builder would duplicate the kite geometry for a difference that is not geometric. Body 0
        // is the tens die and body 1 the units die, fixed by position because the two are
        // interchangeable to the physics and tell apart on screen only by colour (ADR-0034).
        {"D100", makeD10(D10_CIRCUMRADIUS_MM), 2, {Numbering::PercentileTens, Numbering::PercentileUnits}},
    };
    static_assert(sizeof(KINDS) / sizeof(KINDS[0]) == DIE_TYPE_COUNT, "one row per die type");
    return KINDS;
}

} // namespace

const DieKind& dieKind(DieType type) {
    const uint8_t index = static_cast<uint8_t>(type);
    // Outside the enum: a corrupted choice, not a die to guess at.
    if (index >= DIE_TYPE_COUNT) abort();
    return catalog()[index];
}

const char* dieTypeName(DieType type) { return dieKind(type).name; }

uint8_t maxDiceOf(DieType type) {
    const uint8_t bodies = dieKind(type).bodiesPerRoll;
    // A type that throws no bodies could never roll, and would divide by zero here.
    if (bodies == 0) abort();
    return MAX_DICE / bodies;
}

unsigned dieOutcomes(const DieKind& kind) {
    unsigned outcomes = 1;
    for (uint8_t body = 0; body < kind.bodiesPerRoll; body++) outcomes *= kind.shape.faceCount;
    return outcomes;
}

Numbering bodyNumbering(const DieKind& kind, uint8_t body) {
    // A type that throws no bodies could never roll, and would divide by zero here.
    if (kind.bodiesPerRoll == 0) abort();
    return kind.numbering[body % kind.bodiesPerRoll];
}

unsigned dieValue(const DieKind& kind, const uint8_t* numerals) {
    for (uint8_t body = 0; body < kind.bodiesPerRoll; body++) {
        // A shape's numerals run 1 to N, so anything else is a body that never locked or a
        // corrupted shape, and not a result to score from.
        if (numerals[body] == 0 || numerals[body] > kind.shape.faceCount) abort();
    }
    switch (kind.numbering[0]) {
        case Numbering::Plain:
            return numerals[0];
        case Numbering::PercentileTens: {
            const unsigned value = (numerals[0] - 1u) * PERCENTILE_TENS_STEP + (numerals[1] - 1u);
            // A percentile roll runs 1 to 100, so the one outcome that would otherwise be zero is
            // the top of the range: 00 with 0 is 100 (ADR-0034).
            return value == 0 ? PERCENTILE_OUTCOMES : value;
        }
        case Numbering::PercentileUnits:
            // The units body is never the first a die throws, so a catalog row that says so is
            // corrupt rather than a die to score by guesswork.
            break;
    }
    abort();
}

bool parseDieType(const char* name, DieType& type) {
    for (uint8_t index = 0; index < DIE_TYPE_COUNT; index++) {
        if (strcmp(name, catalog()[index].name) != 0) continue;
        type = static_cast<DieType>(index);
        return true;
    }
    return false;
}

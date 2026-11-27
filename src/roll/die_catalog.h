#ifndef DIE_CATALOG_H
#define DIE_CATALOG_H

#include <stdint.h>

#include "../geometry/die_shape.h"
#include "../geometry/die_numbering.h"

// In ascending order of outcomes: PRIMARY steps through them in this order (slice 0026), and the
// dice still to come slot in where their outcome counts put them.
enum class DieType : uint8_t { D4, D6, D8, D10, D12, D20, D100 };
// Counted from the last type: a type added after D100 moves this to it.
constexpr uint8_t DIE_TYPE_COUNT = static_cast<uint8_t>(DieType::D100) + 1;

// The longest a name may be, without its terminator: "D100", the percentile die, is the longest
// the product will ever show. It sizes the buffers that list the names.
constexpr uint8_t DIE_NAME_MAX_CHARS = 4;

/**
 * @brief The most outcomes any type has: the d%'s 100. It sizes the host probe's tallies, which
 * count a die's value and not a body's face, and `test_die_catalog` holds every row inside it.
 */
constexpr uint16_t MAX_DIE_OUTCOMES = 100;

/**
 * @brief Everything that differs between one die type and the next.
 *
 * The one row per type in `die_catalog.cpp` is the product's whole list of dice. Until the fourth
 * type there was a list per question instead (the names, the body to roll, the name a probe's
 * argument spells), which is three places to add a die and three to forget one (slice 0026).
 *
 * It is the whole list for the firmware and for the probe program. The host side of the fairness
 * probe, `scripts/fairness-probe.mjs`, names the dice again, because it carries each one's sample
 * size and critical value too: statistics rather than geometry.
 */
struct DieKind {
    /** @brief What the band shows and a probe's argument spells, such as "D20". */
    const char*    name;
    /** @brief The body rolled for this type, at the edge length the product rolls it at. */
    const DieShape shape;
    /**
     * @brief How many bodies one die of this type throws: one for every die but the d%, which is
     * a tens body and a units body (ADR-0034). The player's count counts dice, so the world holds
     * `count * bodiesPerRoll` bodies and never more than MAX_DICE.
     */
    uint8_t        bodiesPerRoll;
    /**
     * @brief What each body's numerals mean, one entry per body thrown. Entries from
     * `bodiesPerRoll` on are never read.
     */
    Numbering      numbering[MAX_BODIES_PER_DIE];
};

/**
 * @brief The kind for @p type.
 *
 * A value outside the enum aborts: a corrupted choice is not a die to guess at.
 */
const DieKind& dieKind(DieType type);

/** @brief The name of @p type, for the band and the logs. */
const char* dieTypeName(DieType type);

/**
 * @brief The most dice of @p type one roll may throw: MAX_DICE bodies, `bodiesPerRoll` at a time.
 *
 * One for the d%, because two bodies of three leave no room for a second pair, and three for
 * every other type (ADR-0034).
 */
uint8_t maxDiceOf(DieType type);

/**
 * @brief How many values a die of @p kind can score: its faces raised to its bodies, so 20 for a
 * D20 and 100 for the d%. The values themselves are 1 to this, each reachable exactly one way.
 */
unsigned dieOutcomes(const DieKind& kind);

/**
 * @brief What the numerals of world body @p body mean, in a roll of @p kind.
 *
 * Bodies run tens, units, tens, units for a d% and are all plain otherwise, so the numbering
 * repeats every `bodiesPerRoll` bodies (ADR-0034).
 */
Numbering bodyNumbering(const DieKind& kind, uint8_t body);

/**
 * @brief What one die of @p kind scores, from the numerals its @p bodiesPerRoll bodies locked on.
 *
 * A plain die scores its numeral. A d% scores `(tens - 1) * 10 + (units - 1)`, and 00 with 0
 * scores 100, because a percentile roll runs 1 to 100 and not 0 to 99 (ADR-0034). This is the one
 * die whose value is not a numeral, which is why the band is handed a total rather than summing.
 */
unsigned dieValue(const DieKind& kind, const uint8_t* numerals);

/**
 * @brief Sets @p type to the one @p name names and returns true.
 *
 * @return false, leaving @p type untouched, when no type carries that name.
 */
bool parseDieType(const char* name, DieType& type);

#endif // DIE_CATALOG_H

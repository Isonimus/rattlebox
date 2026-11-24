#ifndef DIE_CATALOG_H
#define DIE_CATALOG_H

#include <stdint.h>

#include "../geometry/die_shape.h"

// In ascending order of faces: PRIMARY steps through them in this order (slice 0026), and the dice
// still to come slot in where their face counts put them.
enum class DieType : uint8_t { D4, D6, D8, D10, D12, D20 };
// Counted from the last type: a type added after D20 moves this to it.
constexpr uint8_t DIE_TYPE_COUNT = static_cast<uint8_t>(DieType::D20) + 1;

// The longest a name may be, without its terminator: "D100", the percentile die the roadmap
// plans, is the longest the product will ever show. It sizes the buffers that list the names.
constexpr uint8_t DIE_NAME_MAX_CHARS = 4;

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
 * @brief Sets @p type to the one @p name names and returns true.
 *
 * @return false, leaving @p type untouched, when no type carries that name.
 */
bool parseDieType(const char* name, DieType& type);

#endif // DIE_CATALOG_H

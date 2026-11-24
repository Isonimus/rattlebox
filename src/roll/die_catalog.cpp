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
    static const DieKind KINDS[] = {
        {"D4", makeD4(D4_EDGE_MM)},
        {"D6", makeD6(D6_EDGE_MM)},
        {"D8", makeD8(D8_EDGE_MM)},
        {"D10", makeD10(D10_CIRCUMRADIUS_MM)},
        {"D12", makeD12(D12_EDGE_MM)},
        {"D20", makeD20(D20_EDGE_MM)},
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

bool parseDieType(const char* name, DieType& type) {
    for (uint8_t index = 0; index < DIE_TYPE_COUNT; index++) {
        if (strcmp(name, catalog()[index].name) != 0) continue;
        type = static_cast<DieType>(index);
        return true;
    }
    return false;
}

#ifndef RESULTS_BAND_H
#define RESULTS_BAND_H

#include <stdint.h>
#include "../geometry/die_numbering.h"
#include "../physics/physics_config.h"

/** @brief A segment drawn in the band's own colour, not a die's. */
constexpr uint8_t BAND_NO_DIE = 0xFF;
constexpr uint8_t BAND_SEGMENT_CAPACITY = 8;
// The type, a result per body, and the total. A roll never holds more than MAX_DICE bodies,
// whether those are three dice of one body or one d% of two (ADR-0034).
constexpr uint8_t BAND_MAX_SEGMENTS = 1 + MAX_DICE + 1;

struct BandSegment {
    char    text[BAND_SEGMENT_CAPACITY];
    /** @brief The die whose colour the segment takes, or BAND_NO_DIE. */
    uint8_t die;
};

/**
 * @brief One body's locked result, already resolved through its numbering: "5" for a plain die,
 * "70" for a percentile tens body (ADR-0034).
 */
struct BandResult {
    char text[NUMERAL_LABEL_MAX_CHARS + 1];
};

/** @brief The band's text, left to right. */
struct ResultsBand {
    BandSegment segments[BAND_MAX_SEGMENTS];
    uint8_t     segmentCount;
};

/** @brief The strip before every die of a roll has locked: the type alone, so the player sees what the next roll throws. */
ResultsBand typeBand(const char* typeName);

/**
 * @brief The band shown once every body of a roll has locked (docs/roadmap.md, v0.1): the type,
 * each body's locked result in that body's colour, and for more than one body the roll's @p total.
 * A die can be knocked onto another face after it locks (ADR-0009), so the band, not the face, is
 * the result.
 *
 * The total is handed in rather than summed, because a d%'s two bodies make one die worth 73 and
 * not two dice worth 70 and 3, and because 00 with 0 is 100 (ADR-0034).
 */
ResultsBand resultsBand(const char* typeName, const BandResult* bodies, uint8_t bodyCount, unsigned total);

#endif // RESULTS_BAND_H

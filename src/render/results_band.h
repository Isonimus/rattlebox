#ifndef RESULTS_BAND_H
#define RESULTS_BAND_H

#include <stdint.h>
#include "../physics/physics_config.h"

/** @brief A segment drawn in the band's own colour, not a die's. */
constexpr uint8_t BAND_NO_DIE = 0xFF;
constexpr uint8_t BAND_SEGMENT_CAPACITY = 8;
// The type, a result per die, and the total.
constexpr uint8_t BAND_MAX_SEGMENTS = 1 + MAX_DICE + 1;

struct BandSegment {
    char    text[BAND_SEGMENT_CAPACITY];
    /** @brief The die whose colour the segment takes, or BAND_NO_DIE. */
    uint8_t die;
};

/** @brief The band's text, left to right. */
struct ResultsBand {
    BandSegment segments[BAND_MAX_SEGMENTS];
    uint8_t     segmentCount;
};

/** @brief The strip before every die of a roll has locked: the type alone, so the player sees what the next roll throws. */
ResultsBand typeBand(const char* typeName);

/**
 * @brief The band shown once every die has locked (docs/roadmap.md, v0.1): the type, each die's
 * locked result in its die's colour, and for more than one die the total. A die can be knocked
 * onto another face after it locks (ADR-0009), so the band, not the face, is the result.
 */
ResultsBand resultsBand(const char* typeName, const uint8_t* numerals, uint8_t dieCount);

#endif // RESULTS_BAND_H

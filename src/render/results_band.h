#ifndef RESULTS_BAND_H
#define RESULTS_BAND_H

#include <stdint.h>
#include "../geometry/die_numbering.h"
#include "../physics/physics_config.h"

/** @brief A segment drawn in the band's own colour, not a die's. */
constexpr uint8_t BAND_NO_DIE = 0xFF;

// What the band has room to say (ADR-0036). `drawResultsBand` draws the built-in 6 x 8 font at
// size 2, so a character is 12 px wide, and it starts this far in from the screen's left edge.
// The drawing reads both from here, so the limit below is the limit the band is actually drawn to.
constexpr int BAND_FONT_CHAR_WIDTH_PX = 12;
constexpr int BAND_MARGIN_PX = 4;
/** @brief What `drawResultsBand` leaves after each segment, and each side of a result's chip. */
constexpr int BAND_SEGMENT_GAP_PX = 6;
constexpr int BAND_CHIP_PADDING_PX = 3;
/**
 * @brief The most characters one segment filling the band can show: 19 of the 240 px width.
 *
 * It bounds anything the band says in a single segment, a refused expression's reason above all
 * (ADR-0036): a reason too long to draw is a refusal the player cannot read.
 */
constexpr uint8_t BAND_MESSAGE_MAX_CHARS = (SCREEN_WIDTH_PX - BAND_MARGIN_PX) / BAND_FONT_CHAR_WIDTH_PX;
/** @brief Sized for the longest thing a segment carries, which is a reason and not a result. */
constexpr uint8_t BAND_SEGMENT_CAPACITY = BAND_MESSAGE_MAX_CHARS + 1;
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
    /** @brief A label that outlives the band, as `numeralLabel` returns it. */
    const char* text;
};

/** @brief The band's text, left to right. */
struct ResultsBand {
    BandSegment segments[BAND_MAX_SEGMENTS];
    uint8_t     segmentCount;
};

/**
 * @brief A band of one plain segment carrying @p text, which is every band that is not a result:
 * the type before a roll, an expression being typed, and the reason one was refused (ADR-0036).
 *
 * Text longer than `BAND_MESSAGE_MAX_CHARS` aborts. The band cannot draw it, and a message cut
 * short to fit would refuse an expression for a reason that no longer reads as one.
 */
ResultsBand messageBand(const char* text);

/** @brief The strip before every die of a roll has locked: the type alone, so the player sees what the next roll throws. */
ResultsBand typeBand(const char* typeName);

/**
 * @brief A roll's total, and whether the band shows it.
 *
 * It is handed in rather than summed here, because a d%'s two bodies make one die worth 73 and
 * not two dice worth 70 and 3, and because 00 with 0 is 100 (ADR-0034). It is signed because a
 * modifier can be taken from it: `1d4-9` is a total of -5, and an unsigned one would show that as
 * four billion (ADR-0036).
 */
struct BandTotal {
    int  value;
    /**
     * @brief False when the total would only repeat the band's one result, which is a single
     * plain die with no modifier (slice 0017). A modifier makes the total differ from the result,
     * so a modified roll always shows it, however few dice it threw (ADR-0036).
     */
    bool shown;
};

/**
 * @brief The band shown once every body of a roll has locked (docs/roadmap.md, v0.1): the type,
 * each body's locked result in that body's colour, and the roll's @p total where it says anything
 * the results do not. A die can be knocked onto another face after it locks (ADR-0009), so the
 * band, not the face, is the result.
 */
ResultsBand resultsBand(const char* typeName, const BandResult* bodies, uint8_t bodyCount, BandTotal total);

#endif // RESULTS_BAND_H

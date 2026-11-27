#include "die_numbering.h"

#include <stdlib.h>

#include "die_shape.h"

namespace {

/** @brief How many numerals a percentile body carries: it is a D10 whichever half it is. */
constexpr uint8_t PERCENTILE_BODY_NUMERALS = 10;

// The labels, indexed by numeral - 1. They are tables rather than a format call because the
// renderer asks for one per visible face, per die, per frame, and a label is a pure function of a
// numbering and a numeral over a domain of at most DIE_MAX_FACES values. See numeralLabel's
// comment for what formatting them cost.
constexpr const char* PLAIN_LABELS[] = {"1", "2",  "3",  "4",  "5",  "6",  "7",  "8",  "9",  "10",
                                        "11", "12", "13", "14", "15", "16", "17", "18", "19", "20"};
// A shape's numerals run 1 to its face count, and no shape has more faces than this, so every
// numeral any die can show has a label here.
static_assert(sizeof(PLAIN_LABELS) / sizeof(PLAIN_LABELS[0]) == DIE_MAX_FACES,
              "one plain label per numeral a shape can carry");

/** @brief A tens body shows 00 to 90, two characters always: its lowest face is 00, never 0. */
constexpr const char* PERCENTILE_TENS_LABELS[] = {"00", "10", "20", "30", "40", "50", "60", "70", "80", "90"};
static_assert(sizeof(PERCENTILE_TENS_LABELS) / sizeof(PERCENTILE_TENS_LABELS[0]) == PERCENTILE_BODY_NUMERALS,
              "one tens label per face of a percentile body");

constexpr const char* PERCENTILE_UNITS_LABELS[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
static_assert(sizeof(PERCENTILE_UNITS_LABELS) / sizeof(PERCENTILE_UNITS_LABELS[0]) == PERCENTILE_BODY_NUMERALS,
              "one units label per face of a percentile body");

/** @brief @p labels[numeral - 1], or abort if @p numeral is not one this table carries. */
const char* labelAt(const char* const* labels, uint8_t count, uint8_t numeral) {
    // A shape's numerals start at 1 and stop at its face count, so anything else is a corrupted
    // shape or a body numbered against its declaration, not a label to guess at.
    if (numeral == 0 || numeral > count) abort();
    return labels[numeral - 1];
}

} // namespace

const char* numeralLabel(Numbering numbering, uint8_t numeral) {
    switch (numbering) {
        case Numbering::Plain:
            return labelAt(PLAIN_LABELS, DIE_MAX_FACES, numeral);
        case Numbering::PercentileTens:
            return labelAt(PERCENTILE_TENS_LABELS, PERCENTILE_BODY_NUMERALS, numeral);
        case Numbering::PercentileUnits:
            return labelAt(PERCENTILE_UNITS_LABELS, PERCENTILE_BODY_NUMERALS, numeral);
    }
    // Outside the enum: a corrupted declaration, not a label to guess at.
    abort();
}

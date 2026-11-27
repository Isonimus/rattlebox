#include "die_numbering.h"

#include <stdio.h>
#include <stdlib.h>

namespace {

/** @brief How many numerals a percentile body carries: it is a D10 whichever half it is. */
constexpr uint8_t PERCENTILE_BODY_NUMERALS = 10;

} // namespace

void numeralLabel(Numbering numbering, uint8_t numeral, char* out, uint8_t size) {
    // A shape's numerals start at 1, so 0 is a corrupted shape rather than a face to label.
    if (numeral == 0 || size == 0) abort();
    switch (numbering) {
        case Numbering::Plain:
            snprintf(out, size, "%u", static_cast<unsigned>(numeral));
            return;
        case Numbering::PercentileTens:
            if (numeral > PERCENTILE_BODY_NUMERALS) abort();
            // Two characters always, so 1 reads as "00" and not "0": a tens die's lowest face is
            // the one a printed d% writes as 00 (ADR-0034).
            snprintf(out, size, "%02u", static_cast<unsigned>(numeral - 1) * PERCENTILE_TENS_STEP);
            return;
        case Numbering::PercentileUnits:
            if (numeral > PERCENTILE_BODY_NUMERALS) abort();
            snprintf(out, size, "%u", static_cast<unsigned>(numeral - 1));
            return;
    }
    // Outside the enum: a corrupted declaration, not a label to guess at.
    abort();
}

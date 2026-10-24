#include "results_band.h"

#include <stdio.h>
#include <stdlib.h>

namespace {

void addSegment(ResultsBand& band, uint8_t die, const char* format, unsigned value) {
    BandSegment& segment = band.segments[band.segmentCount++];
    snprintf(segment.text, sizeof segment.text, format, value);
    segment.die = die;
}

} // namespace

ResultsBand resultsBand(const char* typeName, const uint8_t* numerals, uint8_t dieCount) {
    // More dice than the band has room for is a programming error, not a band to quietly cut short.
    if (dieCount == 0 || dieCount > MAX_DICE) abort();
    ResultsBand band{};
    BandSegment& type = band.segments[band.segmentCount++];
    snprintf(type.text, sizeof type.text, "%s", typeName);
    type.die = BAND_NO_DIE;
    unsigned total = 0;
    for (uint8_t die = 0; die < dieCount; die++) {
        addSegment(band, die, "%u", numerals[die]);
        total += numerals[die];
    }
    if (dieCount > 1) addSegment(band, BAND_NO_DIE, "= %u", total);
    return band;
}

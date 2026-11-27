#include "results_band.h"

#include <stdio.h>
#include <stdlib.h>

namespace {

void addSegment(ResultsBand& band, uint8_t die, const char* text) {
    BandSegment& segment = band.segments[band.segmentCount++];
    snprintf(segment.text, sizeof segment.text, "%s", text);
    segment.die = die;
}

} // namespace

ResultsBand typeBand(const char* typeName) {
    ResultsBand band{};
    BandSegment& type = band.segments[band.segmentCount++];
    snprintf(type.text, sizeof type.text, "%s", typeName);
    type.die = BAND_NO_DIE;
    return band;
}

ResultsBand resultsBand(const char* typeName, const BandResult* bodies, uint8_t bodyCount, unsigned total) {
    // More bodies than the band has room for is a programming error, not a band to quietly cut short.
    if (bodyCount == 0 || bodyCount > MAX_DICE) abort();
    ResultsBand band = typeBand(typeName);
    for (uint8_t body = 0; body < bodyCount; body++) addSegment(band, body, bodies[body].text);
    if (bodyCount > 1) {
        char text[BAND_SEGMENT_CAPACITY];
        snprintf(text, sizeof text, "= %u", total);
        addSegment(band, BAND_NO_DIE, text);
    }
    return band;
}

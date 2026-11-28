#include "results_band.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

void addSegment(ResultsBand& band, uint8_t die, const char* text) {
    BandSegment& segment = band.segments[band.segmentCount++];
    snprintf(segment.text, sizeof segment.text, "%s", text);
    segment.die = die;
}

} // namespace

ResultsBand messageBand(const char* text) {
    // A message the band cannot draw is a message the player never reads, and one cut short to
    // fit would say something other than what it was written to say (ADR-0036).
    if (strlen(text) > BAND_MESSAGE_MAX_CHARS) abort();
    ResultsBand band{};
    addSegment(band, BAND_NO_DIE, text);
    return band;
}

ResultsBand typeBand(const char* typeName) { return messageBand(typeName); }

ResultsBand resultsBand(const char* typeName, const BandResult* bodies, uint8_t bodyCount, BandTotal total) {
    // More bodies than the band has room for is a programming error, not a band to quietly cut short.
    if (bodyCount == 0 || bodyCount > MAX_DICE) abort();
    ResultsBand band = typeBand(typeName);
    for (uint8_t body = 0; body < bodyCount; body++) addSegment(band, body, bodies[body].text);
    if (total.shown) {
        char text[BAND_SEGMENT_CAPACITY];
        snprintf(text, sizeof text, "= %d", total.value);
        addSegment(band, BAND_NO_DIE, text);
    }
    return band;
}

#include "frame_stats.h"

#include <stdlib.h>

namespace {

constexpr size_t PERCENTILE_95 = 95;
constexpr size_t PERCENT = 100;

int compareDurations(const void* a, const void* b) {
    const uint32_t left = *static_cast<const uint32_t*>(a);
    const uint32_t right = *static_cast<const uint32_t*>(b);
    return (left > right) - (left < right);
}

/** @brief Index of the nearest-rank percentile: the smallest value with at least p% at or below it. */
size_t nearestRank(size_t count, size_t percentile) {
    return (count * percentile + PERCENT - 1) / PERCENT - 1;
}

} // namespace

PhaseStats summarizeDurations(uint32_t* durationsUs, size_t count) {
    qsort(durationsUs, count, sizeof(uint32_t), compareDurations);
    // The median by nearest rank is the 50th percentile: the lower middle for an even count.
    return {durationsUs[nearestRank(count, PERCENT / 2)], durationsUs[nearestRank(count, PERCENTILE_95)],
            durationsUs[count - 1]};
}

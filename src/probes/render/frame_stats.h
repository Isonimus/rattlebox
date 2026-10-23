#ifndef FRAME_STATS_H
#define FRAME_STATS_H

#include <stddef.h>
#include <stdint.h>

/** @brief The summary the render probe prints for one phase of one run (slice 0006). */
struct PhaseStats {
    uint32_t medianUs;
    uint32_t p95Us;
    uint32_t maxUs;
};

/**
 * @brief Median, 95th percentile and maximum of @p count durations, by nearest rank.
 *
 * Sorts @p durationsUs in place. Nearest rank, not interpolation, so every figure printed
 * is a frame that really happened.
 * @param count at least 1.
 */
PhaseStats summarizeDurations(uint32_t* durationsUs, size_t count);

#endif // FRAME_STATS_H

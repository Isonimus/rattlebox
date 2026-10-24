#ifndef FRAME_PACER_H
#define FRAME_PACER_H

#include <stdint.h>

constexpr uint32_t US_PER_S = 1000000;
constexpr uint32_t FRAMES_PER_S = 60;
/** @brief ADR-0007's 60 fps, rounded to the microsecond. */
constexpr uint32_t FRAME_PERIOD_US = (US_PER_S + FRAMES_PER_S / 2) / FRAMES_PER_S;

/** @brief How a frame that was due at a deadline hands over to the next one. */
struct FramePace {
    /** @brief How long to wait before the next frame starts. */
    uint32_t waitUs;
    /** @brief When the next frame is due. */
    uint32_t nextDeadlineUs;
    /** @brief The frame ended past its deadline. */
    bool     late;
};

/**
 * @brief Pace a frame that ended at @p nowUs and was due by @p deadlineUs (slice 0011).
 *
 * Early, wait for the deadline. Late by less than a frame, start at once and keep the cadence, so
 * the next frame makes the time up. Late by a frame or more (a screen capture holds the loop for
 * seconds), start the cadence over from now: catching up would rush a burst of frames. Times wrap
 * with micros(), so they are compared by their signed difference.
 */
FramePace paceFrame(uint32_t deadlineUs, uint32_t nowUs);

#endif // FRAME_PACER_H

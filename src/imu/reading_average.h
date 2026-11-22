#ifndef READING_AVERAGE_H
#define READING_AVERAGE_H

#include <stdint.h>
#include "../geometry/vec3.h"
#include "imu_sample.h"

// ADR-0014: quick enough that the dice have not slid far by the time a hand settles into the
// reading pose (they slid 0.4 of the tray's depth in 0.25 s), slow enough that a shake's strokes,
// a few per second, still move the dice.
constexpr float READING_AVERAGE_TIME_CONSTANT_S = 0.1f;

/**
 * @brief The device's reading averaged exponentially with a time constant of
 * READING_AVERAGE_TIME_CONSTANT_S (ADR-0014): the angle the device is held at, followed while dice
 * are in flight.
 *
 * Each sample pulls the average toward itself by 1 − e^(−Δt/τ), with Δt from the samples'
 * timestamps, never the configured rate (invariant #4).
 */
class ReadingAverage {
public:
    void add(const ImuSample& sample);
    /** @brief The average, in g; aborts before the first sample. */
    Vec3 meanG() const;

private:
    Vec3     _meanG{0, 0, 0};
    uint32_t _lastUs = 0;
    bool     _started = false;
};

#endif // READING_AVERAGE_H

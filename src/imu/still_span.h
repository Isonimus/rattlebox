#ifndef STILL_SPAN_H
#define STILL_SPAN_H

#include <stdint.h>
#include "../geometry/vec3.h"
#include "imu_sample.h"

// Slice 0012: how long, and how still, the device must be held for its angle to count as level.
constexpr uint32_t STILL_SPAN_US = 250000;
// Measured on the IMU probe's captures at 100 Hz: nine in ten hand-held 0.25 s windows stay within
// 0.089 g of their mean, and the stillest window inside any of 24 recorded shakes reached 0.836 g.
constexpr float STILL_SPREAD_G = 0.1f;
// 0.25 s at ADR-0005's 100 Hz is 25 samples, and the chips were measured up to 1.1% fast. A span
// holding more than this means the IMU runs far faster than configured, which is a fault.
constexpr uint8_t STILL_SPAN_CAPACITY = 32;

/**
 * @brief The device's latest still moment: the mean reading of the last STILL_SPAN_US of samples,
 * kept whenever every one of them is within STILL_SPREAD_G of that mean (slice 0012).
 *
 * The span is measured by the samples' timestamps, never by counting samples at the configured
 * rate (invariant #4).
 */
class StillSpan {
public:
    void add(const ImuSample& sample);
    /** @brief False until a whole span has been still, so a reading taken mid-motion at boot is not a level. */
    bool found() const { return _found; }
    /** @brief The mean reading, in g, of the latest still span; aborts before found(). */
    Vec3 latestMeanG() const;

private:
    uint32_t _timestampsUs[STILL_SPAN_CAPACITY] = {};
    Vec3     _readingsG[STILL_SPAN_CAPACITY] = {};
    uint8_t  _oldest = 0;
    uint8_t  _count = 0;
    bool     _spansWindow = false;
    bool     _found = false;
    Vec3     _latestMeanG{0, 0, 0};
};

#endif // STILL_SPAN_H

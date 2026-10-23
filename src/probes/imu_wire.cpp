#include "imu_wire.h"
#include <math.h>

int16_t packCount(float value, float fullScale) {
    // Rounding, not truncation: a count times the scale and divided back can land a hair
    // under the whole number it started as.
    const long count = lroundf(value / fullScale * FULL_SCALE_COUNTS);
    if (count > INT16_MAX) return INT16_MAX;
    if (count < INT16_MIN) return INT16_MIN;
    return static_cast<int16_t>(count);
}

PackedSample packSample(const ImuSample& sample, ImuFullScale fullScale) {
    return {
        sample.timestampUs,
        {packCount(sample.accelG.x, fullScale.accelG), packCount(sample.accelG.y, fullScale.accelG),
         packCount(sample.accelG.z, fullScale.accelG)},
        {packCount(sample.gyroDps.x, fullScale.gyroDps), packCount(sample.gyroDps.y, fullScale.gyroDps),
         packCount(sample.gyroDps.z, fullScale.gyroDps)},
    };
}

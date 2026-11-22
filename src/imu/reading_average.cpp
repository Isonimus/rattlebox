#include "reading_average.h"

#include <math.h>
#include <stdlib.h>

namespace {

constexpr float MICROSECONDS_PER_SECOND = 1e6f;

} // namespace

void ReadingAverage::add(const ImuSample& sample) {
    if (!_started) {
        _meanG = sample.accelG;
        _started = true;
    } else {
        // Unsigned subtraction keeps the spacing right across the 71-minute wrap of a µs timestamp.
        const float elapsedS = (sample.timestampUs - _lastUs) / MICROSECONDS_PER_SECOND;
        // No NaN check: a reading is an int16 count times a constant full scale (imu_decode), so it
        // is always finite, and one NaN could only come from a decoder bug, not from the chip.
        _meanG = _meanG + (sample.accelG - _meanG) * (1 - expf(-elapsedS / READING_AVERAGE_TIME_CONSTANT_S));
    }
    _lastUs = sample.timestampUs;
}

Vec3 ReadingAverage::meanG() const {
    if (!_started) abort();
    return _meanG;
}

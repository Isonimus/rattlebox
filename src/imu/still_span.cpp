#include "still_span.h"

#include <stdlib.h>

void StillSpan::add(const ImuSample& sample) {
    if (_count > 0) {
        const uint32_t newestUs = _timestampsUs[(_oldest + _count - 1) % STILL_SPAN_CAPACITY];
        if (sample.timestampUs - newestUs >= IMU_SAMPLE_GAP_LIMIT_US) {
            _count = 0;
            _spansWindow = false;
        }
    }
    // Unsigned subtraction keeps the age right across the 71-minute wrap of a µs timestamp.
    while (_count > 0 && sample.timestampUs - _timestampsUs[_oldest] >= STILL_SPAN_US) {
        _oldest = (_oldest + 1) % STILL_SPAN_CAPACITY;
        _count--;
        _spansWindow = true;
    }
    if (_count == STILL_SPAN_CAPACITY) abort();

    const uint8_t slot = (_oldest + _count) % STILL_SPAN_CAPACITY;
    _timestampsUs[slot] = sample.timestampUs;
    _readingsG[slot] = sample.accelG;
    _count++;
    if (!_spansWindow) return;

    Vec3 sum{0, 0, 0};
    for (uint8_t i = 0; i < _count; i++) sum = sum + _readingsG[(_oldest + i) % STILL_SPAN_CAPACITY];
    const Vec3 mean = sum * (1.0f / _count);
    for (uint8_t i = 0; i < _count; i++) {
        if (length(_readingsG[(_oldest + i) % STILL_SPAN_CAPACITY] - mean) > STILL_SPREAD_G) return;
    }
    _latestMeanG = mean;
    _found = true;
}

Vec3 StillSpan::latestMeanG() const {
    // Before any still span there is no level to give, and a made-up one would tilt the tray.
    if (!_found) abort();
    return _latestMeanG;
}

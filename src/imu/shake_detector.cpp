#include "shake_detector.h"

#include <math.h>
#include <stdlib.h>

void ShakeDetector::add(const ImuSample& sample) {
    if (_count > 0) {
        const uint32_t newestUs = _timestampsUs[(_oldest + _count - 1) % SHAKE_WINDOW_CAPACITY];
        if (sample.timestampUs - newestUs >= IMU_SAMPLE_GAP_LIMIT_US) {
            _count = 0;
            _busyCount = 0;
            _spansWindow = false;
        }
    }
    // Unsigned subtraction keeps the age right across the 71-minute wrap of a µs timestamp.
    while (_count > 0 && sample.timestampUs - _timestampsUs[_oldest] >= SHAKE_WINDOW_US) {
        if (_busy[_oldest]) _busyCount--;
        _oldest = (_oldest + 1) % SHAKE_WINDOW_CAPACITY;
        _count--;
        _spansWindow = true;
    }
    if (_count == SHAKE_WINDOW_CAPACITY) abort();

    const bool busy = fabsf(length(sample.accelG) - 1.0f) > SHAKE_BUSY_DEVIATION_G;
    const uint8_t slot = (_oldest + _count) % SHAKE_WINDOW_CAPACITY;
    _timestampsUs[slot] = sample.timestampUs;
    _busy[slot] = busy;
    _count++;
    if (busy) _busyCount++;
}

bool ShakeDetector::shaking() const {
    return _spansWindow && static_cast<float>(_busyCount) > SHAKE_BUSY_SHARE * _count;
}

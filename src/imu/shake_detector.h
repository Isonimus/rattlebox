#ifndef SHAKE_DETECTOR_H
#define SHAKE_DETECTOR_H

#include <stdint.h>
#include "imu_sample.h"

// ADR-0005: a shake is told from handling by how long the motion lasts, not how large it is.
constexpr float SHAKE_BUSY_DEVIATION_G = 0.5f;
constexpr uint32_t SHAKE_WINDOW_US = 1000000;
// The midpoint between the worst shake (0.30) and the worst handling (0.16) measured at 100 Hz.
constexpr float SHAKE_BUSY_SHARE = 0.23f;
// 1 s at ADR-0005's 100 Hz is 100 samples, and the chips were measured up to 1.1% fast. A window
// holding more than this means the IMU runs far faster than configured, which is a fault.
constexpr uint8_t SHAKE_WINDOW_CAPACITY = 128;

/**
 * @brief Whether the device is being shaken: more than SHAKE_BUSY_SHARE of the samples in the
 * last second are over SHAKE_BUSY_DEVIATION_G off 1 g (ADR-0005).
 *
 * The window is measured by the samples' timestamps, never by counting samples at the configured
 * rate (invariant #4).
 */
class ShakeDetector {
public:
    void add(const ImuSample& sample);
    /** @brief False until the samples span a whole window, so the first busy sample after boot is not a shake. */
    bool shaking() const;

private:
    uint32_t _timestampsUs[SHAKE_WINDOW_CAPACITY] = {};
    bool     _busy[SHAKE_WINDOW_CAPACITY] = {};
    uint8_t  _oldest = 0;
    uint8_t  _count = 0;
    uint8_t  _busyCount = 0;
    bool     _spansWindow = false;
};

#endif // SHAKE_DETECTOR_H

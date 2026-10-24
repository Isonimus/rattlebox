#ifndef IMU_SAMPLE_H
#define IMU_SAMPLE_H

#include <stdint.h>
#include "../geometry/vec3.h"

/**
 * @brief One IMU reading on the device frame of ADR-0001: x to the screen's right,
 * y to its top, z out of the screen.
 */
struct ImuSample {
    Vec3     accelG;
    Vec3     gyroDps;
    uint32_t timestampUs;
};

/**
 * @brief A gap between samples this long means the IMU went unread: the screen capture holds the
 * loop for seconds. Five periods at ADR-0005's 100 Hz, and about three times the longest gap
 * between the product's reads (17.2 ms, slice 0012). A window spanning such a gap holds only the few
 * samples after it, and would judge them as if they were all of it, so windows restart instead.
 */
constexpr uint32_t IMU_SAMPLE_GAP_LIMIT_US = 50000;

#endif // IMU_SAMPLE_H

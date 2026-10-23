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

#endif // IMU_SAMPLE_H

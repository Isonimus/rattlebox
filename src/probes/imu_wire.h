#ifndef IMU_WIRE_H
#define IMU_WIRE_H

#include <stdint.h>
#include "../imu/imu_decode.h"
#include "../imu/imu_sample.h"

/**
 * @brief One recorded sample as the IMU probe sends it (slice 0004): the timestamp in µs
 * and the six device-frame values as signed counts of the configured full scale, so a
 * count of ±32768 is ±full scale. Little-endian on the ESP32 and on the wire.
 *
 * Counts rather than floats halve the recording, which is what lets a 5 s window at 1 kHz
 * fit the StickC's heap.
 */
struct PackedSample {
    uint32_t timestampUs;
    int16_t accel[3];
    int16_t gyro[3];
};
static_assert(sizeof(PackedSample) == 16, "the host reads 16-byte records");

/**
 * @brief A device-frame value as a count of @p fullScale.
 *
 * Exact for every value the decoder produced, since each began as a count. The one
 * exception is +full scale, which only negating a −32768 count reaches (an axis the board
 * remaps with a sign flip), and which clamps to 32767.
 */
int16_t packCount(float value, float fullScale);

PackedSample packSample(const ImuSample& sample, ImuFullScale fullScale);

#endif // IMU_WIRE_H

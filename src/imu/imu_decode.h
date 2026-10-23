#ifndef IMU_DECODE_H
#define IMU_DECODE_H

#include <stddef.h>
#include <stdint.h>
#include "imu_sample.h"

/** @brief Acceleration and angular rate on the chip's own axes, before the board remaps them. */
struct SensorReading {
    Vec3 accelG;
    Vec3 gyroDps;
};

/**
 * @brief The size of a raw count at full scale, for a 16-bit signed reading. Shared by
 * the decoder and the IMU probe's packer, which must scale alike (slice 0004).
 */
constexpr float FULL_SCALE_COUNTS = 32768.0f;

/** @brief The ranges a chip was configured for; a raw count of ±FULL_SCALE_COUNTS is full scale. */
struct ImuFullScale {
    float accelG;
    float gyroDps;
};

// One burst read covers both sensors, so accel and gyro come from the same instant.
// BMI270: ACC_X_LSB (0x0C) through GYR_Z_MSB (0x17), little-endian.
constexpr size_t BMI270_BURST_BYTES = 12;
// MPU6886: ACCEL_XOUT_H (0x3B) through GYRO_ZOUT_L (0x48), big-endian, with the
// two temperature bytes between accel and gyro.
constexpr size_t MPU6886_BURST_BYTES = 14;

SensorReading decodeBmi270(const uint8_t (&burst)[BMI270_BURST_BYTES], ImuFullScale fullScale);
SensorReading decodeMpu6886(const uint8_t (&burst)[MPU6886_BURST_BYTES], ImuFullScale fullScale);

#endif // IMU_DECODE_H

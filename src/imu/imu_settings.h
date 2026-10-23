#ifndef IMU_SETTINGS_H
#define IMU_SETTINGS_H

#include <stdint.h>
#include "imu_decode.h"

/**
 * @brief What the IMU is asked to run at, in physical units (slice 0004).
 *
 * Each chip translates this into its own register values and refuses what it cannot do
 * exactly: a rate rounded to the nearest supported one would make the configured and the
 * measured behaviour disagree, which is the mismatch the IMU probe exists to rule out.
 */
struct ImuSettings {
    uint16_t accelRangeG;
    uint16_t gyroRangeDps;
    uint16_t outputRateHz;
};

/** @brief The decode scale for these settings, so it cannot disagree with the chip's range. */
constexpr ImuFullScale fullScaleOf(const ImuSettings& settings) {
    return {static_cast<float>(settings.accelRangeG), static_cast<float>(settings.gyroRangeDps)};
}

/** @brief BMI270 register values: ACC_CONF, ACC_RANGE, GYR_CONF, GYR_RANGE. */
struct Bmi270Registers {
    uint8_t accConf;
    uint8_t accRange;
    uint8_t gyrConf;
    uint8_t gyrRange;
};

/** @brief MPU6886 register values: ACCEL_CONFIG, GYRO_CONFIG, SMPLRT_DIV. */
struct Mpu6886Registers {
    uint8_t accelConfig;
    uint8_t gyroConfig;
    uint8_t sampleRateDivider;
};

/**
 * @brief Translate settings for the BMI270.
 * @return nullptr on success, with @p registers filled; otherwise why the chip cannot
 *         run these settings, naming the field, with @p registers left untouched.
 */
const char* toBmi270Registers(const ImuSettings& settings, Bmi270Registers& registers);

/** @brief As toBmi270Registers, for the MPU6886. */
const char* toMpu6886Registers(const ImuSettings& settings, Mpu6886Registers& registers);

#endif // IMU_SETTINGS_H

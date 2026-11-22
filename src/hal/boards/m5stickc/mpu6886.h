#ifndef MPU6886_H
#define MPU6886_H

#include "../../imu_hal.h"
#include "../../../imu/axis_map.h"
#include "../../../imu/imu_decode.h"
#include "../../../imu/imu_settings.h"

// The StickC Plus 2's internal I2C bus and the IMU's address on it.
constexpr int     MPU6886_SDA_PIN = 21;
constexpr int     MPU6886_SCL_PIN = 22;
constexpr uint8_t MPU6886_ADDRESS = 0x68;

/**
 * @brief The StickC Plus 2's MPU6886 on its internal I2C bus (G21 SDA, G22 SCL).
 *
 * Replaces Dragon Radar's driver, which carried on after a wrong WHO_AM_I and skipped
 * short reads, so it returned stale data instead of failing (slice 0003).
 */
class Mpu6886 : public ImuHAL {
public:
    bool begin() override;
    const char* failureReason() const override { return _failureReason; }
    const char* description() const override { return _description; }
    bool read(ImuSample& sample) override;
    bool hasNewSample(bool& available) override;

private:
    // Set by begin() from the configured settings, so decoding uses the range written.
    ImuFullScale _fullScale{};
    char _failureReason[80] = "";
    char _description[80] = "";

    // Records why begin() failed and returns false, so each check is one line.
    bool fail(const char* format, ...) __attribute__((format(printf, 2, 3)));
    bool waitForData();
};

#endif // MPU6886_H

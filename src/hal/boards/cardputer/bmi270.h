#ifndef BMI270_H
#define BMI270_H

#include "../../imu_hal.h"
#include "../../../imu/axis_map.h"
#include "../../../imu/imu_decode.h"
#include "../../../imu/imu_settings.h"

/**
 * @brief The Cardputer ADV's BMI270, on the internal I2C bus it shares with the keyboard.
 *
 * Init follows Bosch's BMI270 Sensor API (bmi2.c, bmi2_soft_reset and
 * bmi2_write_config_file): reset, upload the config blob, then check the chip's
 * internal status, because a BMI270 whose upload went wrong reads all zeros rather
 * than reporting an error (slice 0003).
 */
class Bmi270 : public ImuHAL {
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
    bool uploadConfig();
    bool configureSensors(const Bmi270Registers& registers);
    bool waitForData();
};

#endif // BMI270_H

#include "imu_settings.h"

namespace {

/** @brief One supported setting and the register bits that select it. */
struct Choice {
    uint16_t value;
    uint8_t bits;
};

template <size_t N>
bool lookUp(const Choice (&choices)[N], uint16_t value, uint8_t& bits) {
    for (const Choice& choice : choices) {
        if (choice.value == value) {
            bits = choice.bits;
            return true;
        }
    }
    return false;
}

// BMI270 datasheet 5.2. ACC_RANGE and GYR_RANGE hold a code per range.
constexpr Choice BMI270_ACCEL_RANGES[] = {{2, 0x00}, {4, 0x01}, {8, 0x02}, {16, 0x03}};
constexpr Choice BMI270_GYRO_RANGES[] = {{2000, 0x00}, {1000, 0x01}, {500, 0x02}, {250, 0x03}, {125, 0x04}};
// The ODR code, low nibble of ACC_CONF and GYR_CONF alike. Only rates both sensors have:
// the gyro starts at 25 Hz, the accelerometer stops at 1600 Hz.
constexpr Choice BMI270_RATES[] = {{25, 0x06}, {50, 0x07}, {100, 0x08}, {200, 0x09},
                                   {400, 0x0A}, {800, 0x0B}, {1600, 0x0C}};
// filter_perf (bit 7) with the normal bandwidth, bwp = 2 (bits 5:4), for both sensors:
// the accelerometer's bwp field is bits 6:4 and the gyro's bit 6 is noise_perf, and 2 in
// bits 5:4 with bit 6 clear means the same in both.
constexpr uint8_t BMI270_CONF_PERF_NORMAL_BANDWIDTH = 0xA0;

// MPU6886 register map: FS_SEL in bits 4:3 of ACCEL_CONFIG and GYRO_CONFIG.
constexpr Choice MPU6886_ACCEL_RANGES[] = {{2, 0x00}, {4, 0x08}, {8, 0x10}, {16, 0x18}};
constexpr Choice MPU6886_GYRO_RANGES[] = {{250, 0x00}, {500, 0x08}, {1000, 0x10}, {2000, 0x18}};
// With the digital low-pass filter on, the chip samples at 1 kHz internally and outputs
// 1 kHz / (1 + SMPLRT_DIV).
constexpr uint16_t MPU6886_INTERNAL_RATE_HZ = 1000;
constexpr uint16_t MPU6886_MAX_DIVIDER = 255;

} // namespace

const char* toBmi270Registers(const ImuSettings& settings, Bmi270Registers& registers) {
    uint8_t accRange = 0;
    uint8_t gyrRange = 0;
    uint8_t rateCode = 0;
    if (!lookUp(BMI270_ACCEL_RANGES, settings.accelRangeG, accRange)) return "the BMI270 has no such accel range";
    if (!lookUp(BMI270_GYRO_RANGES, settings.gyroRangeDps, gyrRange)) return "the BMI270 has no such gyro range";
    if (!lookUp(BMI270_RATES, settings.outputRateHz, rateCode)) return "the BMI270 has no such output rate";
    const uint8_t conf = BMI270_CONF_PERF_NORMAL_BANDWIDTH | rateCode;
    registers = {conf, accRange, conf, gyrRange};
    return nullptr;
}

const char* toMpu6886Registers(const ImuSettings& settings, Mpu6886Registers& registers) {
    uint8_t accelConfig = 0;
    uint8_t gyroConfig = 0;
    if (!lookUp(MPU6886_ACCEL_RANGES, settings.accelRangeG, accelConfig)) return "the MPU6886 has no such accel range";
    if (!lookUp(MPU6886_GYRO_RANGES, settings.gyroRangeDps, gyroConfig)) return "the MPU6886 has no such gyro range";
    const uint16_t rate = settings.outputRateHz;
    if (rate == 0 || MPU6886_INTERNAL_RATE_HZ % rate != 0 || MPU6886_INTERNAL_RATE_HZ / rate - 1 > MPU6886_MAX_DIVIDER) {
        return "the MPU6886 has no such output rate";
    }
    registers = {accelConfig, gyroConfig, static_cast<uint8_t>(MPU6886_INTERNAL_RATE_HZ / rate - 1)};
    return nullptr;
}

#include "mpu6886.h"
#include <Arduino.h>
#include <Wire.h>
#include <stdarg.h>
#include <stdio.h>

namespace {

constexpr int      SDA_PIN = 21;
constexpr int      SCL_PIN = 22;
constexpr uint32_t BUS_FREQUENCY_HZ = 400000;

// Registers and values: MPU6886 datasheet, and M5Unified's MPU6886_Class::begin for
// the sequence.
constexpr uint8_t ADDRESS             = 0x68;
constexpr uint8_t REG_SMPLRT_DIV      = 0x19;
constexpr uint8_t REG_CONFIG          = 0x1A;
constexpr uint8_t REG_GYRO_CONFIG     = 0x1B;
constexpr uint8_t REG_ACCEL_CONFIG    = 0x1C;
constexpr uint8_t REG_ACCEL_CONFIG2   = 0x1D;
constexpr uint8_t REG_INT_STATUS      = 0x3A;
constexpr uint8_t REG_ACCEL_XOUT_H    = 0x3B;
constexpr uint8_t REG_PWR_MGMT_1      = 0x6B;
constexpr uint8_t REG_WHO_AM_I        = 0x75;

constexpr uint8_t WHO_AM_I_MPU6886    = 0x19;
constexpr uint8_t PWR_MGMT_1_RESET    = 0x80;
constexpr uint8_t PWR_MGMT_1_AUTO_CLOCK = 0x01;
constexpr uint32_t RESET_DELAY_MS     = 10;
constexpr uint8_t INT_STATUS_DATA_READY = 0x01;
// Data-ready rises with the first conversion, about 10 ms after waking, while the gyro is
// still starting: on every one of 150 StickC boots that first sample read the gyro as
// exactly zero and z as 0.54 g (slice 0003). So the driver waits out the gyro's start-up
// from sleep, 35 ms in the datasheet and the longer of the two sensors', before it waits
// for data.
constexpr uint32_t GYRO_START_UP_MS = 35;
constexpr uint32_t DATA_READY_TIMEOUT_MS = 200;

struct RegisterSetting {
    uint8_t reg;
    uint8_t value;
};

// 1 kHz internal rate with the 176 Hz low-pass filter, divided by 10 to the 100 Hz
// the BMI270 runs at, so both boards start from the same rate. The M0 IMU probe
// decides the real rate and filtering (ADR-0001).
constexpr RegisterSetting SETTINGS[] = {
    {REG_PWR_MGMT_1, PWR_MGMT_1_AUTO_CLOCK},
    {REG_ACCEL_CONFIG, 0x10},   // +-8 g
    {REG_GYRO_CONFIG, 0x18},    // +-2000 dps
    {REG_CONFIG, 0x01},         // DLPF 176 Hz
    {REG_SMPLRT_DIV, 0x09},     // 1 kHz / (1 + 9) = 100 Hz
    {REG_ACCEL_CONFIG2, 0x00},  // accel DLPF 218 Hz, no averaging
};

// How the chip sits on the StickC Plus 2's board, measured on 2026-10-23 (slice 0003) with
// the display in rotation 1: screen up read chip z +1.004 g, and the screen's top edge up
// read chip x +0.994 g; a counterclockwise twist peaked at chip gz +400 dps. Device x is
// then y cross z, which is chip -y.
constexpr AxisMap STICKC_AXIS_MAP{
    {SensorAxis::Y, AxisSign::Negative},
    {SensorAxis::X, AxisSign::Positive},
    {SensorAxis::Z, AxisSign::Positive},
};
static_assert(isRotation(STICKC_AXIS_MAP), "a mounted chip is rotated, never mirrored");

bool writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool readRegisters(uint8_t reg, uint8_t* data, size_t length) {
    Wire.beginTransmission(ADDRESS);
    Wire.write(reg);
    // Repeated start: release the bus only after the read completes.
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(ADDRESS, length) != length) return false;
    for (size_t i = 0; i < length; i++) data[i] = static_cast<uint8_t>(Wire.read());
    return true;
}

} // namespace

bool Mpu6886::fail(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(_failureReason, sizeof(_failureReason), format, args);
    va_end(args);
    return false;
}

bool Mpu6886::begin() {
    if (!Wire.begin(SDA_PIN, SCL_PIN, BUS_FREQUENCY_HZ)) return fail("I2C bus failed to start");

    uint8_t whoAmI = 0;
    if (!readRegisters(REG_WHO_AM_I, &whoAmI, 1)) return fail("no ACK at 0x%02X", ADDRESS);
    if (whoAmI != WHO_AM_I_MPU6886) return fail("WHO_AM_I read 0x%02X, expected 0x%02X", whoAmI, WHO_AM_I_MPU6886);

    // The reset's own write may go unacknowledged while the chip restarts, so only the
    // writes after it are checked.
    writeRegister(REG_PWR_MGMT_1, PWR_MGMT_1_RESET);
    delay(RESET_DELAY_MS);

    // Each setting is read back: the decode scale assumes these ranges, so one the chip
    // did not take would silently scale every sample wrong.
    for (const RegisterSetting& setting : SETTINGS) {
        uint8_t readBack = 0;
        if (!writeRegister(setting.reg, setting.value) || !readRegisters(setting.reg, &readBack, 1)) {
            return fail("NACK configuring register 0x%02X", setting.reg);
        }
        if (readBack != setting.value) {
            return fail("register 0x%02X read back 0x%02X", setting.reg, readBack);
        }
    }

    // The settings above woke the chip, so the start-up time runs from here at the latest.
    delay(GYRO_START_UP_MS);
    if (!waitForData()) return false;

    snprintf(_description, sizeof(_description),
             "MPU6886 0x%02X accel +-%d g gyro +-%d dps", ADDRESS,
             static_cast<int>(FULL_SCALE.accelG), static_cast<int>(FULL_SCALE.gyroDps));
    return true;
}

bool Mpu6886::waitForData() {
    const uint32_t startMs = millis();
    uint8_t status = 0;
    do {
        if (!readRegisters(REG_INT_STATUS, &status, 1)) return fail("INT_STATUS read NACK");
        if (status & INT_STATUS_DATA_READY) return true;
        delay(1);
    } while (millis() - startMs < DATA_READY_TIMEOUT_MS);
    return fail("no data within %lu ms", static_cast<unsigned long>(DATA_READY_TIMEOUT_MS));
}

bool Mpu6886::read(ImuSample& sample) {
    uint8_t burst[MPU6886_BURST_BYTES];
    if (!readRegisters(REG_ACCEL_XOUT_H, burst, sizeof(burst))) return false;
    const SensorReading reading = decodeMpu6886(burst, FULL_SCALE);
    sample.accelG = remapToDeviceFrame(reading.accelG, STICKC_AXIS_MAP);
    sample.gyroDps = remapToDeviceFrame(reading.gyroDps, STICKC_AXIS_MAP);
    sample.timestampUs = micros();
    return true;
}

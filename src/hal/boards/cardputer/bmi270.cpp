#include "bmi270.h"
#include "../../delay_at_least.h"
#include "bmi270_config.h"
#include "cardputer_i2c.h"
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

namespace {

// Registers and values: Bosch BMI270 Sensor API, bmi2_defs.h (v2.113.0).
constexpr uint8_t ADDRESS              = 0x69;
constexpr uint8_t REG_CHIP_ID          = 0x00;
constexpr uint8_t REG_STATUS           = 0x03;
constexpr uint8_t REG_ACC_X_LSB        = 0x0C;
constexpr uint8_t REG_INTERNAL_STATUS  = 0x21;
constexpr uint8_t REG_ACC_CONF         = 0x40;
constexpr uint8_t REG_ACC_RANGE        = 0x41;
constexpr uint8_t REG_GYR_CONF         = 0x42;
constexpr uint8_t REG_GYR_RANGE        = 0x43;
constexpr uint8_t REG_INIT_CTRL        = 0x59;
constexpr uint8_t REG_INIT_ADDR_0      = 0x5B;
constexpr uint8_t REG_INIT_DATA        = 0x5E;
constexpr uint8_t REG_PWR_CONF         = 0x7C;
constexpr uint8_t REG_PWR_CTRL         = 0x7D;
constexpr uint8_t REG_CMD              = 0x7E;

constexpr uint8_t CHIP_ID              = 0x24;
constexpr uint8_t CMD_SOFT_RESET       = 0xB6;
constexpr uint8_t PWR_CONF_ADV_POWER_SAVE_OFF = 0x00;
constexpr uint8_t INIT_CTRL_START_LOAD = 0x00;
constexpr uint8_t INIT_CTRL_END_LOAD   = 0x01;
constexpr uint8_t INTERNAL_STATUS_MESSAGE_MASK = 0x0F;
constexpr uint8_t INTERNAL_STATUS_INIT_OK      = 0x01;
// Accelerometer, gyroscope and temperature sensor on; the auxiliary interface off.
constexpr uint8_t PWR_CTRL_ACC_GYR_TEMP = 0x0E;
constexpr uint8_t STATUS_DATA_READY    = 0xC0;  // drdy_acc (bit 7) and drdy_gyr (bit 6)

// Bosch's delays: 2 ms after a soft reset, 450 us after leaving advanced power save,
// and up to 20 ms for the chip to finish initializing once the config is loaded.
constexpr uint32_t SOFT_RESET_DELAY_MS        = 2;
constexpr uint32_t POWER_SAVE_EXIT_DELAY_US   = 450;
constexpr uint32_t CONFIG_INIT_DELAY_MS       = 20;
// Until both sensors finish their first conversion the data registers read zero, which
// the first samples after boot showed on the Cardputer ADV (slice 0003). The gyro's
// start-up is the longer, 45 ms in the datasheet, so this leaves a wide margin.
constexpr uint32_t DATA_READY_TIMEOUT_MS      = 200;

// Any even size up to cardputer_i2c::MAX_WRITE_BYTES works; it divides the blob evenly.
constexpr size_t UPLOAD_CHUNK_BYTES = 64;
static_assert(sizeof(BMI270_CONFIG_FILE) % UPLOAD_CHUNK_BYTES == 0, "the blob uploads in whole chunks");
static_assert(UPLOAD_CHUNK_BYTES <= cardputer_i2c::MAX_WRITE_BYTES, "a chunk fits one I2C write");

// How the chip sits on the Cardputer ADV's board, from the orientation checks on the
// device (slice 0003): its axes already match the device frame, measured 2026-10-22 with
// top edge up reading +y and right edge up reading +x.
constexpr AxisMap CARDPUTER_AXIS_MAP{
    {SensorAxis::X, AxisSign::Positive},
    {SensorAxis::Y, AxisSign::Positive},
    {SensorAxis::Z, AxisSign::Positive},
};
static_assert(isRotation(CARDPUTER_AXIS_MAP), "a mounted chip is rotated, never mirrored");

struct RegisterSetting {
    uint8_t reg;
    uint8_t value;
};

bool writeRegister(uint8_t reg, uint8_t value) {
    return cardputer_i2c::writeRegister(ADDRESS, reg, value);
}

bool readRegister(uint8_t reg, uint8_t& value) {
    return cardputer_i2c::readRegister(ADDRESS, reg, value);
}

} // namespace

bool Bmi270::fail(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(_failureReason, sizeof(_failureReason), format, args);
    va_end(args);
    return false;
}

bool Bmi270::begin() {
    if (!_configured) return fail("IMU settings never configured");
    Bmi270Registers registers{};
    const char* refusal = toBmi270Registers(_settings, registers);
    if (refusal != nullptr) {
        return fail("%s: +-%u g, +-%u dps, %u Hz", refusal, _settings.accelRangeG, _settings.gyroRangeDps,
                    _settings.outputRateHz);
    }
    _fullScale = fullScaleOf(_settings);

    const char* busFailure = cardputer_i2c::begin();
    if (busFailure != nullptr) return fail("%s", busFailure);

    uint8_t chipId = 0;
    if (!readRegister(REG_CHIP_ID, chipId)) return fail("no ACK at 0x%02X", ADDRESS);
    if (chipId != CHIP_ID) return fail("CHIP_ID read 0x%02X, expected 0x24", chipId);

    // The reset's own write may go unacknowledged while the chip restarts, so only the
    // writes after it are checked.
    writeRegister(REG_CMD, CMD_SOFT_RESET);
    delayAtLeastMs(SOFT_RESET_DELAY_MS);

    if (!uploadConfig()) return false;
    if (!configureSensors(registers)) return false;
    if (!waitForData()) return false;

    snprintf(_description, sizeof(_description), "BMI270 0x%02X accel +-%u g gyro +-%u dps at %u Hz", ADDRESS,
             _settings.accelRangeG, _settings.gyroRangeDps, _settings.outputRateHz);
    return true;
}

bool Bmi270::uploadConfig() {
    if (!writeRegister(REG_PWR_CONF, PWR_CONF_ADV_POWER_SAVE_OFF)) return fail("PWR_CONF write NACK");
    delayMicroseconds(POWER_SAVE_EXIT_DELAY_US);
    if (!writeRegister(REG_INIT_CTRL, INIT_CTRL_START_LOAD)) return fail("INIT_CTRL write NACK before the upload");

    for (size_t offset = 0; offset < sizeof(BMI270_CONFIG_FILE); offset += UPLOAD_CHUNK_BYTES) {
        // The chip addresses its config memory in 16-bit words, split 4 + 8 bits over
        // INIT_ADDR_0 and INIT_ADDR_1.
        const size_t word = offset / 2;
        const uint8_t wordAddress[2] = {static_cast<uint8_t>(word & 0x0F), static_cast<uint8_t>(word >> 4)};
        const bool written =
            cardputer_i2c::writeRegisters(ADDRESS, REG_INIT_ADDR_0, wordAddress, sizeof(wordAddress)) &&
            cardputer_i2c::writeRegisters(ADDRESS, REG_INIT_DATA, BMI270_CONFIG_FILE + offset, UPLOAD_CHUNK_BYTES);
        if (!written) return fail("config upload NACK at byte %u", static_cast<unsigned>(offset));
    }

    if (!writeRegister(REG_INIT_CTRL, INIT_CTRL_END_LOAD)) return fail("INIT_CTRL write NACK after the upload");
    delayAtLeastMs(CONFIG_INIT_DELAY_MS);

    uint8_t status = 0;
    if (!readRegister(REG_INTERNAL_STATUS, status)) return fail("INTERNAL_STATUS read NACK");
    if ((status & INTERNAL_STATUS_MESSAGE_MASK) != INTERNAL_STATUS_INIT_OK) {
        return fail("config load failed, INTERNAL_STATUS 0x%02X", status);
    }
    return true;
}

bool Bmi270::configureSensors(const Bmi270Registers& registers) {
    const RegisterSetting settings[] = {
        {REG_ACC_CONF, registers.accConf},
        {REG_ACC_RANGE, registers.accRange},
        {REG_GYR_CONF, registers.gyrConf},
        {REG_GYR_RANGE, registers.gyrRange},
    };
    if (!writeRegister(REG_PWR_CTRL, PWR_CTRL_ACC_GYR_TEMP)) return fail("PWR_CTRL write NACK");

    // Each setting is read back: the decode scale assumes these ranges, and the probe these
    // rates, so one the chip did not take would silently make every sample wrong.
    for (const RegisterSetting& setting : settings) {
        uint8_t readBack = 0;
        if (!writeRegister(setting.reg, setting.value) || !readRegister(setting.reg, readBack)) {
            return fail("NACK configuring register 0x%02X", setting.reg);
        }
        if (readBack != setting.value) {
            return fail("register 0x%02X read back 0x%02X, expected 0x%02X", setting.reg, readBack, setting.value);
        }
    }
    return true;
}

bool Bmi270::hasNewSample(bool& available) {
    uint8_t status = 0;
    if (!readRegister(REG_STATUS, status)) return false;
    available = (status & STATUS_DATA_READY) == STATUS_DATA_READY;
    return true;
}

bool Bmi270::waitForData() {
    const uint32_t startMs = millis();
    bool available = false;
    do {
        if (!hasNewSample(available)) return fail("STATUS read NACK");
        if (available) return true;
        delay(1);
    } while (millis() - startMs < DATA_READY_TIMEOUT_MS);
    // STATUS has one ready bit per sensor, so it shows which of the two never converted.
    uint8_t status = 0;
    if (!readRegister(REG_STATUS, status)) return fail("STATUS read NACK");
    return fail("no data within %lu ms, STATUS 0x%02X", static_cast<unsigned long>(DATA_READY_TIMEOUT_MS), status);
}

bool Bmi270::read(ImuSample& sample) {
    uint8_t burst[BMI270_BURST_BYTES];
    if (!cardputer_i2c::readRegisters(ADDRESS, REG_ACC_X_LSB, burst, sizeof(burst))) return false;
    const SensorReading reading = decodeBmi270(burst, _fullScale);
    sample.accelG = remapToDeviceFrame(reading.accelG, CARDPUTER_AXIS_MAP);
    sample.gyroDps = remapToDeviceFrame(reading.gyroDps, CARDPUTER_AXIS_MAP);
    sample.timestampUs = micros();
    return true;
}

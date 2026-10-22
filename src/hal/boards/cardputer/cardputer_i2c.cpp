#include "cardputer_i2c.h"
#include <Wire.h>

namespace cardputer_i2c {

static constexpr int      SDA_PIN = 8;
static constexpr int      SCL_PIN = 9;
static constexpr uint32_t BUS_FREQUENCY_HZ = 400000;

static bool started = false;

bool begin() {
    if (started) return true;
    started = Wire.begin(SDA_PIN, SCL_PIN, BUS_FREQUENCY_HZ);
    return started;
}

bool writeRegister(uint8_t deviceAddress, uint8_t reg, uint8_t value) {
    Wire.beginTransmission(deviceAddress);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool readRegister(uint8_t deviceAddress, uint8_t reg, uint8_t& value) {
    return readRegisters(deviceAddress, reg, &value, 1);
}

bool writeRegisters(uint8_t deviceAddress, uint8_t reg, const uint8_t* data, size_t length) {
    // Wire silently truncates what does not fit its buffer, which would upload half a block.
    if (length > MAX_WRITE_BYTES) return false;
    Wire.beginTransmission(deviceAddress);
    Wire.write(reg);
    Wire.write(data, length);
    return Wire.endTransmission() == 0;
}

bool readRegisters(uint8_t deviceAddress, uint8_t reg, uint8_t* data, size_t length) {
    Wire.beginTransmission(deviceAddress);
    Wire.write(reg);
    // Repeated start: release the bus only after the read completes.
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(deviceAddress, length) != length) return false;
    for (size_t i = 0; i < length; i++) data[i] = static_cast<uint8_t>(Wire.read());
    return true;
}

} // namespace cardputer_i2c

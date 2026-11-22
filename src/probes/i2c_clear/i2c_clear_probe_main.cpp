#include <Arduino.h>
#include <Wire.h>

#include "../../hal/boards/m5stickc/mpu6886.h"
#include "../../hal/gpio_i2c_lines.h"
#include "../../hal/hal.h"

// ADR-0019's probe: does the IMU driver start on a bus the MPU6886 was left driving? A reset mid-read
// leaves it so only now and then, so the probe puts it there every time: it points the chip at
// WHO_AM_I, starts a read, and lets go of the bus a number of clocks into it, as the ESP32's pins
// float at reset. Each stopping point leaves the chip somewhere else: holding its acknowledge, or
// sending a bit of 0x19, which holds SDA low when the bit is 0. Control trials start the driver on
// an idle bus, so a failure is the wedge's, not the probe's.

namespace {

constexpr uint32_t SERIAL_BAUD = 115200;
// The USB bridge enumerates before the monitor attaches; the first lines would be lost without it.
constexpr uint32_t MONITOR_ATTACH_MS = 2000;
constexpr uint8_t  TRIALS_PER_POINT = 5;
constexpr uint32_t HALT_POLL_MS = 1000;
// ADR-0005's product settings, so the driver runs the sequence the product does.
constexpr ImuSettings PRODUCT_IMU_SETTINGS{16, 2000, 100};
constexpr uint8_t BITS_PER_BYTE = 8;
constexpr uint8_t READ_BIT = 0x01;
constexpr uint8_t REG_WHO_AM_I = 0x75;
// Clocks into the data byte the probe lets go at: 0 is the acknowledge of the address, 1 to 8 a
// data bit.
constexpr uint8_t STOPPING_POINTS = BITS_PER_BYTE + 1;

HardwareHAL& hal = getHAL();

/** @brief One clock with SDA released, so a device may drive it. */
void clockReleased(I2cLines& lines) {
    lines.pullSclLow();
    lines.waitHalfClock();
    lines.releaseSda();
    lines.waitHalfClock();
    lines.releaseScl();
    lines.waitHalfClock();
}

/** @brief Clock out @p value, most significant bit first, then the device's acknowledge. */
void sendByte(I2cLines& lines, uint8_t value) {
    for (uint8_t bit = 0; bit < BITS_PER_BYTE; bit++) {
        lines.pullSclLow();
        lines.waitHalfClock();
        if ((value & (0x80 >> bit)) != 0) lines.releaseSda(); else lines.pullSdaLow();
        lines.waitHalfClock();
        lines.releaseScl();
        lines.waitHalfClock();
    }
    clockReleased(lines);
}

/** @brief SDA falling while SCL is high; from a released bus, or after a byte as a repeated START. */
void sendStart(I2cLines& lines) {
    lines.pullSclLow();
    lines.waitHalfClock();
    lines.releaseSda();
    lines.waitHalfClock();
    lines.releaseScl();
    lines.waitHalfClock();
    lines.pullSdaLow();
    lines.waitHalfClock();
}

/** @brief Leave the MPU6886 @p dataClocks clocks into a read of WHO_AM_I; true if it holds SDA low. */
bool wedgeBus(uint8_t dataClocks) {
    Wire.end();
    GpioI2cLines lines(MPU6886_SDA_PIN, MPU6886_SCL_PIN);
    lines.waitHalfClock();
    sendStart(lines);
    sendByte(lines, static_cast<uint8_t>(MPU6886_ADDRESS << 1));
    sendByte(lines, REG_WHO_AM_I);
    sendStart(lines);
    sendByte(lines, static_cast<uint8_t>(MPU6886_ADDRESS << 1) | READ_BIT);
    for (uint8_t clock = 0; clock < dataClocks; clock++) clockReleased(lines);
    return !lines.sdaHigh();
}

/** @brief Start the IMU driver, @p dataClocks into a read or, with a negative count, on an idle bus. */
bool trial(int dataClocks) {
    bool held = false;
    if (dataClocks >= 0) {
        held = wedgeBus(static_cast<uint8_t>(dataClocks));
    } else {
        Wire.end();
    }
    const bool started = hal.imu.begin();
    Serial.printf("[probe] clocks=%d sda_held=%d imu=%s%s%s\n", dataClocks, held, started ? "OK" : "FAILED",
                  started ? "" : " reason=", started ? "" : hal.imu.failureReason());
    return started;
}

} // namespace

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(MONITOR_ATTACH_MS);
    hal.power.begin();
    hal.imu.configure(PRODUCT_IMU_SETTINGS);

    for (uint8_t point = 0; point < STOPPING_POINTS; point++) {
        uint8_t failures = 0;
        uint8_t controlFailures = 0;
        for (uint8_t index = 0; index < TRIALS_PER_POINT; index++) {
            if (!trial(point)) failures++;
            if (!trial(-1)) controlFailures++;
        }
        Serial.printf("[probe] point=%u failed=%u of %u control_failed=%u of %u\n", point, failures,
                      TRIALS_PER_POINT, controlFailures, TRIALS_PER_POINT);
    }
    Serial.println("[probe] done");
}

void loop() { delay(HALT_POLL_MS); }

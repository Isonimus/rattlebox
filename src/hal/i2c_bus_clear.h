#ifndef I2C_BUS_CLEAR_H
#define I2C_BUS_CLEAR_H

#include <stdint.h>

/**
 * @brief The two lines of an I2C bus, driven open drain: a line is released to its pull-up, or
 * pulled low. Read back, a released line is low while a device holds it.
 */
class I2cLines {
public:
    virtual ~I2cLines() = default;
    virtual void releaseScl() = 0;
    virtual void pullSclLow() = 0;
    virtual void releaseSda() = 0;
    virtual void pullSdaLow() = 0;
    virtual bool sclHigh() = 0;
    virtual bool sdaHigh() = 0;
    /** @brief Half of a standard-mode (100 kHz) clock period, which every I2C device keeps up with. */
    virtual void waitHalfClock() = 0;
};

// A device cut off mid-transfer finishes its byte and the acknowledge bit after it within eight
// clocks and one (I2C specification UM10204, section 3.1.16, "Bus clear").
constexpr uint8_t I2C_BUS_CLEAR_CLOCKS = 9;

/**
 * @brief Free a bus a device was left driving, before the I2C controller takes it (ADR-0019).
 *
 * A reset stops the ESP32 mid-transfer without touching the devices, so a device cut off mid-read
 * goes on sending its byte at the controller's first clocks, and the boot's first transfer fails.
 * Nine clocks with SDA released, then a STOP, end the transfer it thinks is still open.
 *
 * @return null once the bus is idle, and why it is not otherwise.
 */
const char* clearI2cBus(I2cLines& lines);

#endif // I2C_BUS_CLEAR_H

#include "i2c_bus_clear.h"

// A device cut off while being written to, one bit before its acknowledge slot, is back there when
// the nine clocks end, so it acknowledges in the low clock the STOP begins with and the STOP does
// not take. That clock ends the slot, so the second one does.
static constexpr uint8_t STOP_ATTEMPTS = 2;

/** @brief One clock pulse: SCL low, then released, for a half period each. */
static void pulseScl(I2cLines& lines) {
    lines.pullSclLow();
    lines.waitHalfClock();
    lines.releaseScl();
    lines.waitHalfClock();
}

/** @brief SDA rising while SCL is high: the STOP every device takes as the end of a transfer. */
static void sendStop(I2cLines& lines) {
    // SDA changes only while SCL is low, so the fall is a data bit, not a START.
    lines.pullSclLow();
    lines.waitHalfClock();
    lines.pullSdaLow();
    lines.waitHalfClock();
    lines.releaseScl();
    lines.waitHalfClock();
    lines.releaseSda();
    lines.waitHalfClock();
}

const char* clearI2cBus(I2cLines& lines) {
    lines.releaseScl();
    lines.releaseSda();
    lines.waitHalfClock();
    // No device drives SCL except to stretch a clock, which no transfer is in to need: held low, the
    // line is shorted or a device is broken, and clocks cannot free it.
    if (!lines.sclHigh()) return "SCL held low";

    // Every clock, not only while SDA is low: a device mid-read that is sending a 1 holds nothing now
    // but drives its next bit on the next clock, and the probe's one failing stopping point was such
    // a bit (ADR-0019). With SDA released, every device reaches its acknowledge slot, reads a NACK and
    // stops sending.
    for (uint8_t clock = 0; clock < I2C_BUS_CLEAR_CLOCKS; clock++) pulseScl(lines);
    for (uint8_t attempt = 0; attempt < STOP_ATTEMPTS; attempt++) {
        sendStop(lines);
        if (lines.sdaHigh()) break;
    }
    if (!lines.sclHigh()) return "SCL held low after the bus clear";
    if (!lines.sdaHigh()) return "SDA held low after the bus clear";
    return nullptr;
}

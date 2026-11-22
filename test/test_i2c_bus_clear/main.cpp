#include <unity.h>
#include <initializer_list>
#include "../../src/hal/i2c_bus_clear.h"
#include "../../src/hal/i2c_bus_clear.cpp"

// ADR-0019: the bus clear run before each I2C controller starts, against a device modelled bit by
// bit, cut off by a reset at every point of a transfer.

// A data byte's eight bits, then the acknowledge slot.
static const uint8_t ACK_SLOT = 8;
static const uint16_t BYTE_VALUES = 256;

enum class DeviceMode { Idle, Sending, Receiving };

/**
 * @brief A bus with one device on it, as the I2C specification has a device behave: it shifts its
 * next bit out after SCL falls, samples on SCL rising, and takes SDA changing while SCL is high as a
 * START (falling) or STOP (rising).
 */
class SimulatedBus : public I2cLines {
public:
    DeviceMode mode = DeviceMode::Idle;
    uint8_t    byteSent = 0;
    // 0 to 7 is a data bit, most significant first; ACK_SLOT is the acknowledge.
    uint8_t    bit = 0;
    bool       sdaStuckLow = false;
    bool       sclStuckLow = false;
    uint16_t   starts = 0;
    uint16_t   stops = 0;
    uint16_t   sclPulls = 0;

    /** @brief The lines as the reset left them: the master released, SCL at @p sclHighAtReset. */
    void cutOffAt(DeviceMode cutMode, uint8_t cutByte, uint8_t cutBit, bool sclHighAtReset) {
        mode = cutMode;
        byteSent = cutByte;
        bit = cutBit;
        _masterScl = sclHighAtReset;
        _scl = sclHighAtReset;
        _sda = level(_masterSda);
    }

    void releaseScl() override { setScl(true); }
    void pullSclLow() override {
        sclPulls++;
        setScl(false);
    }
    void releaseSda() override { setSda(true); }
    void pullSdaLow() override { setSda(false); }
    bool sclHigh() override { return _scl; }
    bool sdaHigh() override { return _sda; }
    void waitHalfClock() override {}

private:
    bool _masterScl = true;
    bool _masterSda = true;
    bool _scl = true;
    bool _sda = true;

    bool deviceHoldsSda() const {
        if (sdaStuckLow) return true;
        if (mode == DeviceMode::Sending) return bit < ACK_SLOT && (byteSent & (0x80 >> bit)) == 0;
        if (mode == DeviceMode::Receiving) return bit == ACK_SLOT;
        return false;
    }

    bool level(bool masterSda) const { return masterSda && !deviceHoldsSda(); }

    void setScl(bool released) {
        _masterScl = released;
        const bool scl = released && !sclStuckLow;
        if (scl == _scl) return;
        _scl = scl;
        if (scl) {
            sample();
        } else if (mode != DeviceMode::Idle) {
            bit = bit == ACK_SLOT ? 0 : bit + 1;
        }
        // The device changes SDA only here, with SCL low, so its own bits are never a START or STOP.
        _sda = level(_masterSda);
    }

    /** @brief A sending device reads the acknowledge: a NACK ends its transfer. */
    void sample() {
        if (mode == DeviceMode::Sending && bit == ACK_SLOT && _sda) mode = DeviceMode::Idle;
    }

    void setSda(bool released) {
        _masterSda = released;
        const bool sda = level(released);
        if (sda == _sda) return;
        _sda = sda;
        if (!_scl) return;
        if (sda) {
            stops++;
            mode = DeviceMode::Idle;
        } else {
            starts++;
        }
    }
};

static void assertFreed(SimulatedBus& bus, const char* failure) {
    TEST_ASSERT_NULL(failure);
    TEST_ASSERT_TRUE(bus.mode == DeviceMode::Idle);
    TEST_ASSERT_TRUE(bus.sclHigh());
    TEST_ASSERT_TRUE(bus.sdaHigh());
    // A START would open a transfer of its own, to whatever address the clocks then spelled.
    TEST_ASSERT_EQUAL_UINT16(0, bus.starts);
    TEST_ASSERT_EQUAL_UINT16(1, bus.stops);
}

void test_an_idle_bus_stays_idle() {
    SimulatedBus bus;
    assertFreed(bus, clearI2cBus(bus));
}

void test_a_device_cut_off_while_sending_is_freed_at_every_bit_of_every_byte() {
    // The failed boots: the product polls the IMU so often that a reset lands mid-read. Bits that
    // are 1 hold nothing yet, so a clear that stopped once SDA rose would leave the device sending;
    // on the StickC, the probe's one failing stopping point was such a bit (ADR-0019).
    for (uint16_t value = 0; value < BYTE_VALUES; value++) {
        for (uint8_t bit = 0; bit <= ACK_SLOT; bit++) {
            for (const bool sclHigh : {true, false}) {
                SimulatedBus bus;
                bus.cutOffAt(DeviceMode::Sending, static_cast<uint8_t>(value), bit, sclHigh);
                assertFreed(bus, clearI2cBus(bus));
            }
        }
    }
}

void test_a_device_cut_off_while_receiving_is_freed_at_every_bit() {
    // A reset mid-write: the device acknowledges a byte the clocks finish, then waits for the next.
    for (uint8_t bit = 0; bit <= ACK_SLOT; bit++) {
        for (const bool sclHigh : {true, false}) {
            SimulatedBus bus;
            bus.cutOffAt(DeviceMode::Receiving, 0, bit, sclHigh);
            assertFreed(bus, clearI2cBus(bus));
        }
    }
}

void test_sda_held_through_the_clocks_fails() {
    // Shorted, or a device that is broken: a boot that went on would fail at its first transfer
    // with a bus error that names no cause.
    SimulatedBus bus;
    bus.sdaStuckLow = true;
    TEST_ASSERT_EQUAL_STRING("SDA held low after the bus clear", clearI2cBus(bus));
}

void test_scl_held_low_fails_before_any_clock() {
    SimulatedBus bus;
    bus.sclStuckLow = true;
    TEST_ASSERT_EQUAL_STRING("SCL held low", clearI2cBus(bus));
    TEST_ASSERT_EQUAL_UINT16(0, bus.sclPulls);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_an_idle_bus_stays_idle);
    RUN_TEST(test_a_device_cut_off_while_sending_is_freed_at_every_bit_of_every_byte);
    RUN_TEST(test_a_device_cut_off_while_receiving_is_freed_at_every_bit);
    RUN_TEST(test_sda_held_through_the_clocks_fails);
    RUN_TEST(test_scl_held_low_fails_before_any_clock);
    return UNITY_END();
}

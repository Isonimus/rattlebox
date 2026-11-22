#ifndef CARDPUTER_I2C_H
#define CARDPUTER_I2C_H

#include <Arduino.h>

/**
 * @brief The Cardputer ADV's internal I2C bus (G8 SDA, G9 SCL).
 *
 * The keyboard controller, audio codec and IMU all sit on it, so it is
 * started in one place instead of by whichever driver happens to run first.
 */
namespace cardputer_i2c {

/** @brief Start the bus once; later calls are no-ops. Returns null once started, and why not otherwise. */
const char* begin();

/** @brief Write one 8-bit register. Returns false if the device did not ACK. */
bool writeRegister(uint8_t deviceAddress, uint8_t reg, uint8_t value);

/** @brief Read one 8-bit register into @p value. Returns false on any bus error. */
bool readRegister(uint8_t deviceAddress, uint8_t reg, uint8_t& value);

/** @brief Write @p length bytes from @p reg on, in one transaction. False on a NACK or overlong write. */
bool writeRegisters(uint8_t deviceAddress, uint8_t reg, const uint8_t* data, size_t length);

/** @brief Read @p length bytes from @p reg on, in one transaction. False on any bus error or short read. */
bool readRegisters(uint8_t deviceAddress, uint8_t reg, uint8_t* data, size_t length);

/** @brief The most bytes one writeRegisters() call can carry; Wire buffers 128 including the register. */
constexpr size_t MAX_WRITE_BYTES = 127;

} // namespace cardputer_i2c

#endif // CARDPUTER_I2C_H

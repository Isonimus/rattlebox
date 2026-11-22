#ifndef GPIO_I2C_LINES_H
#define GPIO_I2C_LINES_H

#include <Arduino.h>
#include <driver/gpio.h>
#include "i2c_bus_clear.h"

/**
 * @brief An I2C bus's two pins as open-drain GPIOs, for clearI2cBus before Wire takes them
 * (ADR-0019). Wire.begin claims the pins back from GPIO.
 */
class GpioI2cLines : public I2cLines {
public:
    GpioI2cLines(int sdaPin, int sclPin) : _sdaPin(sdaPin), _sclPin(sclPin) {
        // The output level is set before the pin becomes an output, so neither line is pulled low on
        // the way: SDA falling while SCL is high would be a START.
        for (const int pin : {sdaPin, sclPin}) {
            gpio_set_level(static_cast<gpio_num_t>(pin), 1);
            // Wire enables the same internal pull-ups; they hold a released line high meanwhile.
            pinMode(pin, OUTPUT_OPEN_DRAIN | PULLUP);
        }
    }

    void releaseScl() override { digitalWrite(_sclPin, HIGH); }
    void pullSclLow() override { digitalWrite(_sclPin, LOW); }
    void releaseSda() override { digitalWrite(_sdaPin, HIGH); }
    void pullSdaLow() override { digitalWrite(_sdaPin, LOW); }
    bool sclHigh() override { return digitalRead(_sclPin) == HIGH; }
    bool sdaHigh() override { return digitalRead(_sdaPin) == HIGH; }
    void waitHalfClock() override { delayMicroseconds(STANDARD_MODE_HALF_CLOCK_US); }

private:
    // 100 kHz, standard mode.
    static constexpr uint32_t STANDARD_MODE_HALF_CLOCK_US = 5;
    int _sdaPin;
    int _sclPin;
};

#endif // GPIO_I2C_LINES_H

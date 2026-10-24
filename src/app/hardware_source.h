#ifndef HARDWARE_SOURCE_H
#define HARDWARE_SOURCE_H

#include <bootloader_random.h>
#include <esp_random.h>
#include "../hal/startable.h"
#include "../physics/launch.h"

/**
 * @brief The device's launch randomness: the hardware RNG (ADR-0002).
 *
 * esp_random() is a true RNG only while the radio is on or after bootloader_random_enable().
 * Neither radio is used, so begin() turns the entropy source on and leaves it on. While it is on
 * it holds the SAR ADC: nothing reads the ADC today, and the first thing that does must deal with
 * this (slice 0011).
 */
class HardwareSource : public UniformSource, public Startable {
public:
    bool begin() override {
        bootloader_random_enable();
        return true;
    }
    const char* failureReason() const override { return ""; }
    const char* description() const override { return "esp_random with the SAR ADC as entropy"; }

    float nextUnit() override {
        // The top 24 bits fill a float's mantissa exactly, so every value is in [0, 1).
        constexpr int FLOAT_MANTISSA_BITS = 24;
        constexpr int RANDOM_BITS = 32;
        constexpr float UNIT_PER_STEP = 1.0f / (1u << FLOAT_MANTISSA_BITS);
        return static_cast<float>(esp_random() >> (RANDOM_BITS - FLOAT_MANTISSA_BITS)) * UNIT_PER_STEP;
    }
};

#endif // HARDWARE_SOURCE_H

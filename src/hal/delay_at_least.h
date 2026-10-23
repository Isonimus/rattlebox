#ifndef DELAY_AT_LEAST_H
#define DELAY_AT_LEAST_H

#include <Arduino.h>

/**
 * @brief Wait at least @p ms milliseconds, for a datasheet minimum.
 *
 * Arduino-ESP32's delay(n) is vTaskDelay, which returns at the nth tick boundary, so it can
 * wait up to a tick less than n ms: on the Cardputer ADV delay(2) took 1587 to 2450 µs over
 * 200 calls. The BMI270 refuses a write sent under 2 ms after a soft reset, so a plain
 * delay(2) failed boot whenever the call landed late in a tick (slice 0004). One more tick
 * makes the wait a true minimum.
 */
inline void delayAtLeastMs(uint32_t ms) {
    delay(ms + portTICK_PERIOD_MS);
}

#endif // DELAY_AT_LEAST_H

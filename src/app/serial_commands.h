#ifndef SERIAL_COMMANDS_H
#define SERIAL_COMMANDS_H

#include <stdint.h>

/**
 * @brief What a byte from the host asks of the product (slice 0011). One byte each, like the screen
 * capture's 'S', so the host speaks one protocol; 'S' stays the capture's own.
 */
enum class SerialCommand : uint8_t {
    None,
    /** @brief 'R': launch the current choice again. */
    Roll,
    /** @brief 'T': what PRIMARY does, step to the next die type. */
    PressPrimary,
    /** @brief 'C': what SECONDARY does, cycle the count. */
    PressSecondary,
};

SerialCommand serialCommandFor(int byte);

#endif // SERIAL_COMMANDS_H

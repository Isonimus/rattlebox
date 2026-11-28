#ifndef SERIAL_COMMANDS_H
#define SERIAL_COMMANDS_H

#include <stdint.h>

#include "../hal/input_hal.h"

/**
 * @brief What a byte from the host asks of the product (slice 0011). One byte each, like the screen
 * capture's 'S', so the host speaks one protocol; 'S' stays the capture's own.
 */
enum class SerialCommand : uint8_t {
    None,
    /** @brief 'R': launch the current choice again. */
    Roll,
    /** @brief 'T': what PRIMARY does, step to the next die type, or submit a typed expression. */
    PressPrimary,
    /** @brief 'C': what SECONDARY does, cycle the count, or delete a character. */
    PressSecondary,
    /**
     * @brief A character of dice notation, typed as the keyboard types it (ADR-0036).
     *
     * Enter and Backspace are not their own commands: they are PRIMARY and SECONDARY on the
     * keyboard too, and the entry reinterprets those. So the host drives the same path the player
     * does, which is what lets a verify script type an expression with nobody at the keyboard.
     */
    Type,
    /** @brief 'H': what LONG_PRIMARY does, recall the next saved preset and roll it (ADR-0038). */
    HoldPrimary,
    /** @brief 'K': what LONG_SECONDARY does, keep the current choice in the next preset slot. */
    HoldSecondary,
};

/** @brief A command and, for `Type`, the character it carries; `NO_CHARACTER` otherwise. */
struct SerialRequest {
    SerialCommand command;
    char          character;
};

SerialRequest serialCommandFor(int byte);

#endif // SERIAL_COMMANDS_H

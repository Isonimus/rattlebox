#ifndef INPUT_HAL_H
#define INPUT_HAL_H

#include <Arduino.h>
#include "startable.h"

/**
 * @brief Unified input actions across all boards.
 * Keyboard keys, physical buttons, and touch gestures all
 * map to these abstract actions.
 */
enum class InputAction : uint8_t {
    NONE,
    PRIMARY,           // Confirm / select / tap
    SECONDARY,         // Back / alt / double-tap
    UP,                // Navigate up
    DOWN,              // Navigate down
    LONG_PRIMARY,      // Long press on primary
    LONG_SECONDARY     // Long press on secondary
};

/**
 * @brief Nothing was typed: the queue is empty, or the board has no keyboard to type on.
 *
 * A board with two buttons genuinely types nothing, so this is not a failure to be loud about. It
 * is the reason the expression entry is unreachable there (ADR-0036).
 */
constexpr char NO_CHARACTER = '\0';

/**
 * @brief Abstract input interface.
 */
class InputHAL : public Startable {
public:
    /** @brief Poll hardware and latch new input state. Call once per frame. */
    virtual void update() = 0;

    /** @brief Return the highest-priority action detected this frame. */
    virtual InputAction getAction() = 0;

    /**
     * @brief Take the oldest character typed since the last call, or `NO_CHARACTER`.
     *
     * Characters queue where actions latch: an action repeated inside one frame is the same action
     * to the player, but a dropped keystroke is a character they typed and never saw (ADR-0036).
     * The caller drains it to `NO_CHARACTER` every frame.
     */
    virtual char getTypedCharacter() = 0;
};

#endif // INPUT_HAL_H

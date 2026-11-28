#ifndef KEYBOARD_INPUT_H
#define KEYBOARD_INPUT_H

#include "../../input_hal.h"
#include "../../press_gesture.h"
#include "tca8418_keymap.h"

/**
 * @brief One thing the keyboard produced: a character typed, or an action completed.
 *
 * Exactly one of the two is set, because a key either types or acts and never both.
 */
struct KeyInput {
    char        character;
    InputAction action;
};

/**
 * @brief How many key events the controller can hand over in one drain: its event count field is
 * four bits wide (TCA8418 datasheet, "Key Lock and Event Counter"), whatever its FIFO holds.
 */
constexpr uint8_t KEY_EVENTS_PER_DRAIN = 15;

/**
 * @brief What the Cardputer's keys mean, with no I2C in sight.
 *
 * It holds the key bindings, their gestures, whether Shift is down, and the queue of what the
 * keyboard has produced. The driver reads the controller and hands the decoded events here, so
 * everything a keypress decides is host-testable (`test/test_keyboard_input`) rather than
 * reachable only with a board in hand.
 *
 * **Characters and actions come out of one queue, in the order the keys were pressed.** They used
 * to be two channels, a queue of characters and one latched action, and the caller drained the
 * characters first: a Backspace between two characters then deleted the second instead of the
 * first, and a second action in the same batch was dropped altogether. Both need several events in
 * one drain, which is a frame that ran long, and a screen capture blocks for exactly that long
 * (slice 0037).
 */
class KeyboardInput {
public:
    /** @brief Feed one decoded key event, read at @p nowMs. */
    void take(const CardputerKeyEvent& event, uint32_t nowMs);

    /**
     * @brief Let the gestures see the clock, so a long press completes while its key is still held.
     *
     * The driver calls this every frame, not only when the controller reports an event.
     */
    void tick(uint32_t nowMs);

    /** @brief The next character, or `NO_CHARACTER` when the oldest thing waiting is an action. */
    char nextCharacter();

    /** @brief The next action, or `NONE` when the oldest thing waiting is a character. */
    InputAction nextAction();

    /**
     * @brief How many inputs have been dropped since the last call, and clears the count.
     *
     * The queue holds a whole drain's worth, so anything dropped means the caller is not reading
     * it at all. That is a keystroke the player made and never saw, so the driver says so.
     */
    uint8_t takeDropped();

    /** @brief Whether Shift is held, which is what tells a '=' from a '+'. */
    bool shiftHeld() const { return _shiftHeld; }

private:
    static constexpr uint32_t LONG_PRESS_MS = 1000;
    static constexpr uint8_t BINDING_COUNT = 4;
    // A drain's events are at most one input each, and a tick can complete one long press per
    // bound key, so nothing a single frame can produce is ever dropped.
    static constexpr uint8_t CAPACITY = KEY_EVENTS_PER_DRAIN + BINDING_COUNT;

    struct KeyBinding {
        CardputerKey key;
        PressGesture gesture;
        bool         held;
    };

    KeyBinding _bindings[BINDING_COUNT] = {
        {cardputer_keys::ENTER, PressGesture{InputAction::PRIMARY, InputAction::LONG_PRIMARY, LONG_PRESS_MS}, false},
        {cardputer_keys::BACKSPACE, PressGesture{InputAction::SECONDARY, InputAction::LONG_SECONDARY, LONG_PRESS_MS}, false},
        {cardputer_keys::UP, PressGesture{InputAction::UP, InputAction::UP, LONG_PRESS_MS}, false},
        {cardputer_keys::DOWN, PressGesture{InputAction::DOWN, InputAction::DOWN, LONG_PRESS_MS}, false},
    };

    KeyInput _queue[CAPACITY] = {};
    uint8_t  _oldest = 0;
    uint8_t  _count = 0;
    uint8_t  _dropped = 0;
    bool     _shiftHeld = false;

    void push(KeyInput input);
    KeyInput takeOldest();
};

#endif // KEYBOARD_INPUT_H

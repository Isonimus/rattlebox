#ifndef CARDPUTER_INPUT_H
#define CARDPUTER_INPUT_H

#include "../../input_hal.h"
#include "keyboard_input.h"
#include "tca8418_keymap.h"

/**
 * @brief Cardputer ADV keyboard, read from its TCA8418 controller over I2C.
 *
 * Enter and Backspace mirror the M5StickC's two buttons; the arrows navigate:
 *   Enter     → PRIMARY   (hold: LONG_PRIMARY)
 *   Backspace → SECONDARY (hold: LONG_SECONDARY)
 *   ↑ / ↓     → UP / DOWN (a hold counts as one step)
 *
 * The keys dice notation spells also report the character they typed (ADR-0036). Enter and
 * Backspace keep their actions and the expression entry reinterprets them, because what a key
 * means in a given mode is the app's business and which key moved is the driver's.
 *
 * This class is the I2C half only: what a key means lives in `KeyboardInput`, which has no
 * hardware in it and is tested on the host.
 *
 * The controller queues key events and pulls its INT line low while any are
 * pending, so a frame with no keypress costs one digitalRead and no I2C traffic.
 */
class CardputerInput : public InputHAL {
public:
    bool begin() override;
    const char* failureReason() const override { return _failureReason; }
    const char* description() const override { return "TCA8418 keyboard at 0x34"; }
    void update() override;
    InputAction getAction() override { return _keyboard.nextAction(); }
    char getTypedCharacter() override { return _keyboard.nextCharacter(); }

private:
    static constexpr int KEYBOARD_INT_PIN = 11;

    KeyboardInput _keyboard;
    const char*   _failureReason = "";

    bool fail(const char* reason);

    void drainKeyEvents();
};

#endif // CARDPUTER_INPUT_H

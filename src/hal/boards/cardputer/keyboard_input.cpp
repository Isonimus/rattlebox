#include "keyboard_input.h"

void KeyboardInput::push(KeyInput input) {
    // A press that completes no gesture, and a key that types nothing, produced nothing: queueing
    // them would make the queue's order depend on keys that did not do anything.
    if (input.character == NO_CHARACTER && input.action == InputAction::NONE) return;
    if (_count >= CAPACITY) {
        _dropped++;
        return;
    }
    _queue[(_oldest + _count) % CAPACITY] = input;
    _count++;
}

KeyInput KeyboardInput::takeOldest() {
    const KeyInput input = _queue[_oldest];
    _oldest = (_oldest + 1) % CAPACITY;
    _count--;
    return input;
}

void KeyboardInput::take(const CardputerKeyEvent& event, uint32_t nowMs) {
    // Shift is read first, because it changes what the keys pressed after it type, and it is
    // released like any other key. A release lost to an I2C failure leaves it held and the digits
    // typing nothing until it is pressed again; the failed read says so on the serial port.
    if (event.key == cardputer_keys::SHIFT) {
        _shiftHeld = event.pressed;
        return;
    }

    // A tap can queue its press and release within one drain; feeding each event straight to the
    // gesture keeps that tap from being lost.
    for (KeyBinding& binding : _bindings) {
        if (!(event.key == binding.key)) continue;
        binding.held = event.pressed;
        push(KeyInput{NO_CHARACTER, binding.gesture.update(binding.held, nowMs)});
        return;
    }

    // A character is typed when its key goes down. A release types nothing, or every keystroke
    // would arrive twice.
    if (!event.pressed) return;
    push(KeyInput{cardputerCharacter(event.key, _shiftHeld), InputAction::NONE});
}

void KeyboardInput::tick(uint32_t nowMs) {
    for (KeyBinding& binding : _bindings) {
        push(KeyInput{NO_CHARACTER, binding.gesture.update(binding.held, nowMs)});
    }
}

char KeyboardInput::nextCharacter() {
    if (_count == 0 || _queue[_oldest].character == NO_CHARACTER) return NO_CHARACTER;
    return takeOldest().character;
}

InputAction KeyboardInput::nextAction() {
    if (_count == 0 || _queue[_oldest].action == InputAction::NONE) return InputAction::NONE;
    return takeOldest().action;
}

uint8_t KeyboardInput::takeDropped() {
    const uint8_t dropped = _dropped;
    _dropped = 0;
    return dropped;
}

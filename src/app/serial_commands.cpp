#include "serial_commands.h"

#include "../roll/dice_notation.h"

SerialRequest serialCommandFor(int byte) {
    switch (byte) {
        case 'R': return {SerialCommand::Roll, NO_CHARACTER};
        case 'T': return {SerialCommand::PressPrimary, NO_CHARACTER};
        case 'C': return {SerialCommand::PressSecondary, NO_CHARACTER};
        // The holds, which no key sequence can stand in for: a hold is a press held past a
        // threshold, and the host cannot hold a button down over a serial port. 'K' for keep,
        // because 'S' is the screen capture's own.
        case 'H': return {SerialCommand::HoldPrimary, NO_CHARACTER};
        case 'K': return {SerialCommand::HoldSecondary, NO_CHARACTER};
        default: break;
    }
    // The command letters are upper case and the only upper-case character the grammar accepts is
    // 'D', so a typed character can never shadow a command.
    const char character = static_cast<char>(byte);
    if (byte == static_cast<int>(character) && isNotationCharacter(character)) {
        return {SerialCommand::Type, character};
    }
    return {SerialCommand::None, NO_CHARACTER};
}

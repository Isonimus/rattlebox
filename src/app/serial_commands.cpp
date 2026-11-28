#include "serial_commands.h"

#include "../roll/dice_notation.h"

SerialRequest serialCommandFor(int byte) {
    switch (byte) {
        case 'R': return {SerialCommand::Roll, NO_CHARACTER};
        case 'T': return {SerialCommand::PressPrimary, NO_CHARACTER};
        case 'C': return {SerialCommand::PressSecondary, NO_CHARACTER};
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

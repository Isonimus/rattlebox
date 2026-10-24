#include "serial_commands.h"

SerialCommand serialCommandFor(int byte) {
    switch (byte) {
        case 'R': return SerialCommand::Roll;
        case 'T': return SerialCommand::PressPrimary;
        case 'C': return SerialCommand::PressSecondary;
        default: return SerialCommand::None;
    }
}

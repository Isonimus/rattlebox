#include "dice_choice.h"

#include "../physics/physics_config.h"

namespace {

constexpr uint8_t MIN_DICE = 1;

} // namespace

ChoiceChange applyAction(DiceChoice choice, InputAction action) {
    DiceChoice next = choice;
    switch (action) {
        case InputAction::PRIMARY:
            next.type = choice.type == DieType::D20 ? DieType::D6 : DieType::D20;
            break;
        case InputAction::SECONDARY:
            next.count = choice.count == MAX_DICE ? MIN_DICE : choice.count + 1;
            break;
        case InputAction::UP:
            if (choice.count < MAX_DICE) next.count = choice.count + 1;
            break;
        case InputAction::DOWN:
            if (choice.count > MIN_DICE) next.count = choice.count - 1;
            break;
        case InputAction::LONG_PRIMARY:
        case InputAction::LONG_SECONDARY:
        case InputAction::NONE:
            break;
    }
    return {next, next.type != choice.type || next.count != choice.count};
}

const char* dieTypeName(DieType type) { return type == DieType::D20 ? "D20" : "D6"; }

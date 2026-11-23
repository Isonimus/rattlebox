#include "dice_choice.h"

#include "../physics/physics_config.h"

namespace {

constexpr uint8_t MIN_DICE = 1;
// Indexed by DieType. Unsized, so a type with no name fails the assert rather than reading null.
constexpr const char* DIE_TYPE_NAMES[] = {"D6", "D8", "D20"};
static_assert(sizeof(DIE_TYPE_NAMES) / sizeof(DIE_TYPE_NAMES[0]) == DIE_TYPE_COUNT, "one name per die type");

DieType nextType(DieType type) { return static_cast<DieType>((static_cast<uint8_t>(type) + 1) % DIE_TYPE_COUNT); }

} // namespace

ChoiceChange applyAction(DiceChoice choice, InputAction action) {
    DiceChoice next = choice;
    switch (action) {
        case InputAction::PRIMARY:
            next.type = nextType(choice.type);
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

const char* dieTypeName(DieType type) { return DIE_TYPE_NAMES[static_cast<uint8_t>(type)]; }

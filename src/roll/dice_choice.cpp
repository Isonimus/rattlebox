#include "dice_choice.h"

#include "../physics/physics_config.h"

namespace {

constexpr uint8_t MIN_DICE = 1;

DieType nextType(DieType type) { return static_cast<DieType>((static_cast<uint8_t>(type) + 1) % DIE_TYPE_COUNT); }

} // namespace

ChoiceChange applyAction(DiceChoice choice, InputAction action) {
    DiceChoice next = choice;
    switch (action) {
        case InputAction::PRIMARY:
            next.type = nextType(choice.type);
            break;
        // The bound here is the type's own, not MAX_DICE, and it is not a duplicate of the clamp
        // after this switch. The clamp is for PRIMARY, which can land on a type that holds fewer
        // dice; this bound is for the count controls, which cannot change type. Today they agree
        // for every type, because the only narrow one holds a single die and so has nothing to
        // cycle. They would part on a type holding two: with MAX_DICE here, SECONDARY would go
        // 2 to 3, the clamp would put it back to 2, and the count would stick there instead of
        // cycling round to 1. No such type exists to test that, which is why it is written down
        // (ADR-0034).
        case InputAction::SECONDARY:
            next.count = choice.count >= maxDiceOf(choice.type) ? MIN_DICE : choice.count + 1;
            break;
        case InputAction::UP:
            if (choice.count < maxDiceOf(choice.type)) next.count = choice.count + 1;
            break;
        case InputAction::DOWN:
            if (choice.count > MIN_DICE) next.count = choice.count - 1;
            break;
        case InputAction::LONG_PRIMARY:
        case InputAction::LONG_SECONDARY:
        case InputAction::NONE:
            break;
    }
    // A type whose die is more than one body holds fewer dice, so stepping onto it brings the
    // count down to what the tray has room for rather than launching bodies there is no room for
    // (ADR-0034). Stepping back off it leaves the count where the clamp put it, because a count
    // the player can no longer see is a count they did not choose.
    const uint8_t maxDice = maxDiceOf(next.type);
    if (next.count > maxDice) next.count = maxDice;
    return {next, next.type != choice.type || next.count != choice.count};
}

#include "simulated_roll.h"

namespace {

bool everyDieLocked(const RollOutcome& outcome) {
    for (uint8_t die = 0; die < outcome.dieCount; die++) {
        if (outcome.results[die].state != DieState::Locked) return false;
    }
    return true;
}

} // namespace

RollOutcome simulateRoll(const DieShape& shape, uint8_t dieCount, UniformSource& source,
                         const RestThresholds& thresholds, float timeLimitS, StepObserver* observer) {
    World world{};
    world.shape = &shape;
    world.gravity = {0, 0, -GRAVITY_MM_PER_S2};
    launchDice(world, PRODUCT_TRAY, dieCount, source);

    RollOutcome outcome{};
    outcome.dieCount = dieCount;
    for (uint8_t die = 0; die < dieCount; die++) {
        outcome.launchNumerals[die] = shape.faces[upFace(shape, world.dice[die].orientation).face].numeral;
        outcome.results[die] = startRoll();
    }

    const uint32_t stepLimit = stepsIn(timeLimitS);
    for (uint32_t step = 1; step <= stepLimit; step++) {
        stepWorld(world, PRODUCT_TRAY);
        if (observer != nullptr) observer->onStep(world);
        for (uint8_t die = 0; die < dieCount; die++) {
            outcome.results[die] = updateResult(outcome.results[die], world.dice[die], shape, thresholds);
            if (outcome.results[die].state != DieState::Cocked) continue;
            nudgeCockedDie(world.dice[die], source);
            outcome.results[die] = startRoll();
            outcome.nudges++;
        }
        outcome.seconds = step * PHYSICS_STEP_S;
        if (everyDieLocked(outcome)) {
            outcome.settled = true;
            for (uint8_t die = 0; die < dieCount; die++) {
                outcome.shownNumerals[die] = shape.faces[upFace(shape, world.dice[die].orientation).face].numeral;
            }
            return outcome;
        }
    }
    return outcome;
}

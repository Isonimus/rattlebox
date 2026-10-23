#include "simulated_roll.h"

bool everyDieLocked(const DieResult* results, uint8_t dieCount) {
    for (uint8_t die = 0; die < dieCount; die++) {
        if (results[die].state != DieState::Locked) return false;
    }
    return true;
}

uint8_t advanceResults(World& world, DieResult* results, UniformSource& source, const RestThresholds& thresholds) {
    uint8_t nudges = 0;
    for (uint8_t die = 0; die < world.dieCount; die++) {
        results[die] = updateResult(results[die], world.dice[die], *world.shape, thresholds);
        if (results[die].state != DieState::Cocked) continue;
        nudgeCockedDie(world.dice[die], source);
        results[die] = startRoll();
        nudges++;
    }
    return nudges;
}

RollOutcome simulateRoll(const DieShape& shape, uint8_t dieCount, UniformSource& source,
                         const RestThresholds& thresholds, float timeLimitS, StepObserver* observer) {
    World world{};
    world.shape = &shape;
    return simulateRollIn(world, dieCount, source, thresholds, timeLimitS, observer);
}

RollOutcome simulateRollIn(World& world, uint8_t dieCount, UniformSource& source, const RestThresholds& thresholds,
                           float timeLimitS, StepObserver* observer) {
    const DieShape& shape = *world.shape;
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
        outcome.nudges += advanceResults(world, outcome.results, source, thresholds);
        outcome.seconds = step * PHYSICS_STEP_S;
        if (everyDieLocked(outcome.results, dieCount)) {
            outcome.settled = true;
            for (uint8_t die = 0; die < dieCount; die++) {
                outcome.shownNumerals[die] = shape.faces[upFace(shape, world.dice[die].orientation).face].numeral;
            }
            return outcome;
        }
    }
    return outcome;
}

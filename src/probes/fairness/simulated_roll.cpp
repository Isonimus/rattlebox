#include "simulated_roll.h"

RollOutcome simulateRoll(const DieShape& shape, uint8_t dieCount, UniformSource& source,
                         const RestThresholds& thresholds, float timeLimitS, StepObserver* observer) {
    World world{};
    world.shape = &shape;
    return simulateRollIn(world, dieCount, source, thresholds, timeLimitS, observer);
}

RollOutcome simulateRollIn(World& world, uint8_t dieCount, UniformSource& source, const RestThresholds& thresholds,
                           float timeLimitS, StepObserver* observer) {
    RollOutcome outcome{};
    outcome.dieCount = dieCount;
    launchRoll(world, outcome.results, dieCount, source);
    for (uint8_t die = 0; die < dieCount; die++) outcome.launchNumerals[die] = shownNumeral(world, die);

    const uint32_t stepLimit = stepsIn(timeLimitS);
    for (uint32_t step = 1; step <= stepLimit; step++) {
        // stepRoll's two halves, with the observer between them, where it has always watched.
        stepWorld(world, PRODUCT_TRAY);
        if (observer != nullptr) observer->onStep(world);
        outcome.nudges += advanceResults(world, outcome.results, source, thresholds);
        outcome.seconds = step * PHYSICS_STEP_S;
        if (everyDieLocked(outcome.results, dieCount)) {
            outcome.settled = true;
            for (uint8_t die = 0; die < dieCount; die++) outcome.shownNumerals[die] = shownNumeral(world, die);
            return outcome;
        }
    }
    return outcome;
}

#include "dice_roll.h"

uint8_t shownNumeral(const World& world, uint8_t die) {
    const DieShape& shape = *world.shape;
    return shape.faces[upFace(shape, world.dice[die].orientation).face].numeral;
}

bool everyDieLocked(const DieResult* results, uint8_t dieCount) {
    for (uint8_t die = 0; die < dieCount; die++) {
        if (results[die].state != DieState::Locked) return false;
    }
    return true;
}

void launchRoll(World& world, DieResult* results, uint8_t dieCount, UniformSource& source) {
    world.gravity = {0, 0, -GRAVITY_MM_PER_S2};
    launchDice(world, PRODUCT_TRAY, dieCount, source);
    for (uint8_t die = 0; die < dieCount; die++) results[die] = startRoll();
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

uint8_t stepRoll(World& world, DieResult* results, UniformSource& source, const RestThresholds& thresholds) {
    stepWorld(world, PRODUCT_TRAY);
    return advanceResults(world, results, source, thresholds);
}

#ifndef DICE_ROLL_H
#define DICE_ROLL_H

#include <stdint.h>
#include "../physics/die_result.h"
#include "../physics/launch.h"
#include "../physics/world.h"

// How a roll advances, one physics step at a time. The device, the fairness test and the fairness
// probe all call these, so invariant #2 tests the code the device runs (slice 0011).

/** @brief The numeral on @p die's face toward the glass: what the player sees, locked or not. */
uint8_t shownNumeral(const World& world, uint8_t die);

bool everyDieLocked(const DieResult* results, uint8_t dieCount);

/**
 * @brief Throw @p dieCount dice of @p world's shape from rest onto a level tray (ADR-0009) and
 * start every result over.
 */
void launchRoll(World& world, DieResult* results, uint8_t dieCount, UniformSource& source);

/**
 * @brief Advance every die's result by one step, nudging each die that came to rest cocked and
 * starting its result over (ADR-0009). Returns how many dice were nudged.
 */
uint8_t advanceResults(World& world, DieResult* results, UniformSource& source, const RestThresholds& thresholds);

/** @brief One step of a roll left alone: the world, then the results. Returns how many dice were nudged. */
uint8_t stepRoll(World& world, DieResult* results, UniformSource& source, const RestThresholds& thresholds);

#endif // DICE_ROLL_H

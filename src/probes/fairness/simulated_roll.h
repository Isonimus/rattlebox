#ifndef SIMULATED_ROLL_H
#define SIMULATED_ROLL_H

#include <stdint.h>
#include "../../geometry/die_shape.h"
#include "../../physics/die_result.h"
#include "../../physics/launch.h"
#include "../../physics/world.h"

/** @brief Watches every step of a roll; the probe records speeds through it. */
class StepObserver {
public:
    virtual ~StepObserver() = default;
    virtual void onStep(const World& world) = 0;
};

struct RollOutcome {
    /** @brief The numeral facing the glass at launch, to show how much the physics, not the launch, decided. */
    uint8_t   launchNumerals[MAX_DICE];
    DieResult results[MAX_DICE];
    /**
     * @brief The numeral facing the glass when the roll ends. A locked die another die knocks over
     * keeps its result (invariant #3), so this can differ from it; the probe counts how often.
     */
    uint8_t   shownNumerals[MAX_DICE];
    uint8_t   dieCount;
    /** @brief How many times a die came to rest cocked and was nudged. */
    uint16_t  nudges;
    float     seconds;
    /** @brief Every die locked before the time limit. */
    bool      settled;
};

/**
 * @brief One roll left alone on a level tray, as ADR-0002's fairness test runs it: launch,
 * then step until every die is locked, nudging any that come to rest cocked, or until @p timeLimitS.
 */
RollOutcome simulateRoll(const DieShape& shape, uint8_t dieCount, UniformSource& source,
                         const RestThresholds& thresholds, float timeLimitS, StepObserver* observer);

#endif // SIMULATED_ROLL_H

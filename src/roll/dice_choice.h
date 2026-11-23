#ifndef DICE_CHOICE_H
#define DICE_CHOICE_H

#include <stdint.h>
#include "../hal/input_hal.h"
#include "die_catalog.h"

/** @brief What the next roll throws: one type, one to MAX_DICE dice (docs/roadmap.md, v0.1). */
struct DiceChoice {
    DieType type;
    uint8_t count;
};

/** @brief One D20 until NVS presets arrive in v0.2. */
constexpr DiceChoice BOOT_CHOICE{DieType::D20, 1};

struct ChoiceChange {
    DiceChoice choice;
    /** @brief The choice differs from before: the caller launches the new dice. */
    bool       changed;
};

/**
 * @brief The choice after @p action (slice 0011). PRIMARY steps to the next type and from the last
 * back to the first (slice 0026); SECONDARY cycles the count 1, 2, 3, 1, so the StickC's two
 * buttons reach every choice; the Cardputer's UP and DOWN add or take one die, stopping at the ends. Anything else leaves the choice as it was.
 */
ChoiceChange applyAction(DiceChoice choice, InputAction action);

#endif // DICE_CHOICE_H

#ifndef CARDPUTER_POWER_H
#define CARDPUTER_POWER_H

#include "../../power_hal.h"

/**
 * @brief Cardputer ADV power: a hardware switch and no hold pin, so there is nothing to latch.
 */
class CardputerPower : public PowerHAL {
public:
    bool begin() override { return true; }
    const char* failureReason() const override { return ""; }
    const char* description() const override { return "hardware switch, no latch"; }
};

#endif // CARDPUTER_POWER_H

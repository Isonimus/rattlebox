#ifndef M5STICKC_POWER_H
#define M5STICKC_POWER_H

#include "../../power_hal.h"

/**
 * @brief M5StickC Plus 2 power latch: the board stays on only while the HOLD pin (GPIO 4)
 * is driven high, so it is latched first thing at boot.
 */
class M5StickCPower : public PowerHAL {
public:
    bool begin() override;
    const char* failureReason() const override { return ""; }
    const char* description() const override { return "HOLD latch on G4"; }

private:
    static constexpr int HOLD_PIN = 4;
};

#endif // M5STICKC_POWER_H

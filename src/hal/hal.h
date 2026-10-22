#ifndef HAL_H
#define HAL_H

#include "display_hal.h"
#include "input_hal.h"
#include "imu_hal.h"
#include "power_hal.h"

/**
 * @brief Composite HAL — groups all hardware interfaces into one handle.
 * Each board provides a getHAL() implementation that returns
 * references to static, board-specific concrete objects.
 */
struct HardwareHAL {
    const char* boardName;
    DisplayHAL& display;
    InputHAL&   input;
    ImuHAL&     imu;
    PowerHAL&   power;
};

/**
 * @brief Factory function implemented once per board.
 * Returns the singleton HardwareHAL for the active build target.
 */
HardwareHAL& getHAL();

#endif // HAL_H

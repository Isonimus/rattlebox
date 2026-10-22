#ifndef POWER_HAL_H
#define POWER_HAL_H

#include "startable.h"

/**
 * @brief The board's power latch, where it has one. Battery reading and shutdown stay
 * behind in Dragon Radar until a slice uses them (slice 0003).
 */
class PowerHAL : public Startable {};

#endif // POWER_HAL_H

#ifndef DISPLAY_HAL_H
#define DISPLAY_HAL_H

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "startable.h"

/**
 * @brief Abstract display interface.
 * Each board provides a concrete implementation with its own
 * LovyanGFX panel configuration and pin assignments.
 */
class DisplayHAL : public Startable {
public:
    /** @brief Access the underlying LovyanGFX device for drawing. */
    virtual lgfx::LGFX_Device& getDevice() = 0;
};

#endif // DISPLAY_HAL_H

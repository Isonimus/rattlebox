#ifndef TRAY_GRAVITY_H
#define TRAY_GRAVITY_H

#include "../geometry/quaternion.h"
#include "../geometry/vec3.h"

/**
 * @brief The turn that makes the device's holding angle level: it carries @p levelAccelG, a still
 * accelerometer reading, onto the glass's normal. People hold the device tilted toward their face,
 * and without it the dice would pile against the bottom edge (docs/roadmap.md).
 */
Quaternion levelFrom(Vec3 levelAccelG);

/**
 * @brief What the dice fall by, in the tray's frame, from an accelerometer reading in g.
 *
 * A die loose in a moving box feels gravity minus the box's own acceleration, which is exactly
 * the accelerometer's reading negated: one reading gives both the tilt and the shake, with no
 * sensor fusion. The device's rotation is left out, so a turn of the device carries the dice with it.
 */
Vec3 trayGravity(Quaternion level, Vec3 accelG);

#endif // TRAY_GRAVITY_H

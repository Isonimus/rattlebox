#ifndef ROTATION_H
#define ROTATION_H

#include "vec3.h"

/** @brief A rotation as a 3 × 3 matrix, stored as its rows. */
struct Rotation {
    Vec3 rows[3];
};

constexpr Rotation IDENTITY_ROTATION{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

constexpr Vec3 rotate(const Rotation& rotation, Vec3 v) {
    return {dot(rotation.rows[0], v), dot(rotation.rows[1], v), dot(rotation.rows[2], v)};
}

/** @brief The rotation by @p angleRad about @p axis, right-handed; @p axis need not be unit length. */
Rotation rotationAbout(Vec3 axis, float angleRad);

#endif // ROTATION_H

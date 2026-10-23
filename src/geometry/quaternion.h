#ifndef QUATERNION_H
#define QUATERNION_H

#include "rotation.h"
#include "vec3.h"

/**
 * @brief A unit quaternion, the orientation the physics integrates.
 *
 * A body's orientation is updated every step; a matrix updated that way drifts away from a
 * rotation, while a quaternion needs only renormalising. The renderer takes a Rotation, so
 * toRotation converts once per frame.
 */
struct Quaternion {
    float w;
    float x;
    float y;
    float z;
};

constexpr Quaternion IDENTITY_QUATERNION{1, 0, 0, 0};

constexpr Quaternion operator*(Quaternion a, Quaternion b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

Quaternion normalized(Quaternion q);

/** @brief The rotation by @p angleRad about @p axis, right-handed; @p axis need not be unit length. */
Quaternion quaternionAbout(Vec3 axis, float angleRad);

Vec3 rotate(Quaternion q, Vec3 v);

Rotation toRotation(Quaternion q);

/** @brief @p q turned by angular velocity @p omegaRadPerS (world frame) for @p seconds, renormalised. */
Quaternion integrate(Quaternion q, Vec3 omegaRadPerS, float seconds);

#endif // QUATERNION_H

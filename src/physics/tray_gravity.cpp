#include "tray_gravity.h"

#include <stdlib.h>
#include "physics_config.h"

namespace {

constexpr Vec3 GLASS_NORMAL{0, 0, 1};
// Below this, a reading points so nearly opposite the glass's normal that their cross product is
// too short to give a turning axis.
constexpr float OPPOSITE_TOLERANCE = 1e-6f;

} // namespace

Quaternion levelFrom(Vec3 levelAccelG) {
    // A still device reads 1 g; a zero reading is a dead or free-falling sensor, and no tilt can
    // be taken from it.
    if (length(levelAccelG) == 0) abort();
    const Vec3 up = normalized(levelAccelG);
    const float alignment = 1 + dot(up, GLASS_NORMAL);
    // Held face down, every turn of half a revolution about a horizontal axis levels it.
    if (alignment < OPPOSITE_TOLERANCE) return {0, 1, 0, 0};
    // The shortest turn from up to the normal: half its angle is encoded by adding the identity.
    const Vec3 axis = cross(up, GLASS_NORMAL);
    return normalized(Quaternion{alignment, axis.x, axis.y, axis.z});
}

Vec3 trayGravity(Quaternion level, Vec3 accelG) { return rotate(level, accelG) * -GRAVITY_MM_PER_S2; }

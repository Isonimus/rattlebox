#include "quaternion.h"

#include <math.h>

Quaternion normalized(Quaternion q) {
    const float scale = 1.0f / sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return {q.w * scale, q.x * scale, q.y * scale, q.z * scale};
}

Quaternion quaternionAbout(Vec3 axis, float angleRad) {
    const Vec3 k = normalized(axis) * sinf(angleRad / 2);
    return {cosf(angleRad / 2), k.x, k.y, k.z};
}

Vec3 rotate(Quaternion q, Vec3 v) {
    // v' = v + 2w(u × v) + 2u × (u × v), with u the vector part: q v q* without building q*.
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

Rotation toRotation(Quaternion q) {
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    return {{
        {1 - 2 * (yy + zz), 2 * (xy - wz), 2 * (xz + wy)},
        {2 * (xy + wz), 1 - 2 * (xx + zz), 2 * (yz - wx)},
        {2 * (xz - wy), 2 * (yz + wx), 1 - 2 * (xx + yy)},
    }};
}

Quaternion integrate(Quaternion q, Vec3 omegaRadPerS, float seconds) {
    // dq/dt = ½ (0, ω) q for ω in the world frame.
    const Quaternion spin = Quaternion{0, omegaRadPerS.x, omegaRadPerS.y, omegaRadPerS.z} * q;
    const float half = seconds / 2;
    return normalized({q.w + spin.w * half, q.x + spin.x * half, q.y + spin.y * half, q.z + spin.z * half});
}

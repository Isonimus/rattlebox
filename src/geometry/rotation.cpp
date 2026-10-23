#include "rotation.h"

Rotation rotationAbout(Vec3 axis, float angleRad) {
    // Rodrigues' formula: R = cI + s[k]x + (1 - c)kk^T.
    const Vec3 k = normalized(axis);
    const float c = cosf(angleRad);
    const float s = sinf(angleRad);
    const float t = 1.0f - c;
    return {{
        {t * k.x * k.x + c, t * k.x * k.y - s * k.z, t * k.x * k.z + s * k.y},
        {t * k.x * k.y + s * k.z, t * k.y * k.y + c, t * k.y * k.z - s * k.x},
        {t * k.x * k.z - s * k.y, t * k.y * k.z + s * k.x, t * k.z * k.z + c},
    }};
}

#ifndef AXIS_MAP_H
#define AXIS_MAP_H

#include <stdint.h>
#include "imu_sample.h"

enum class SensorAxis : uint8_t { X, Y, Z };
enum class AxisSign : int8_t { Positive = 1, Negative = -1 };

/** @brief Where one device axis comes from: a sensor axis, possibly reversed. */
struct AxisSource {
    SensorAxis axis;
    AxisSign   sign;
};

/**
 * @brief How a board's IMU is mounted: for each device axis (ADR-0001), its sensor axis.
 * Each board declares one constant; only the orientation checks on the device can prove
 * that constant matches how the chip is soldered (slice 0003).
 */
struct AxisMap {
    AxisSource x;
    AxisSource y;
    AxisSource z;
};

/**
 * @brief True when the map is a rotation: each sensor axis used once, and no mirroring.
 *
 * A chip is mounted by rotating it, never by mirroring it, so a mirrored map is a typo.
 * It also matters for correctness: the same map is applied to acceleration and to
 * angular rate, and that is only valid for a rotation, because a mirror reverses the
 * sense of rotation.
 */
constexpr bool isRotation(const AxisMap& map) {
    const int a = static_cast<int>(map.x.axis);
    const int b = static_cast<int>(map.y.axis);
    const int c = static_cast<int>(map.z.axis);
    const bool isPermutation = a != b && b != c && a != c;
    // (0,1,2) shifted cyclically keeps the handedness; any other order swaps two axes.
    const bool evenPermutation = (b == (a + 1) % 3) && (c == (b + 1) % 3);
    const int signProduct = static_cast<int>(map.x.sign) * static_cast<int>(map.y.sign) *
                            static_cast<int>(map.z.sign);
    const int determinant = (evenPermutation ? 1 : -1) * signProduct;
    return isPermutation && determinant == 1;
}

Vec3 remapToDeviceFrame(const Vec3& sensor, const AxisMap& map);

#endif // AXIS_MAP_H

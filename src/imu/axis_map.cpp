#include "axis_map.h"

namespace {

float pick(const Vec3& sensor, AxisSource source) {
    float value = 0.0f;
    switch (source.axis) {
        case SensorAxis::X: value = sensor.x; break;
        case SensorAxis::Y: value = sensor.y; break;
        case SensorAxis::Z: value = sensor.z; break;
    }
    return source.sign == AxisSign::Negative ? -value : value;
}

} // namespace

Vec3 remapToDeviceFrame(const Vec3& sensor, const AxisMap& map) {
    return {pick(sensor, map.x), pick(sensor, map.y), pick(sensor, map.z)};
}

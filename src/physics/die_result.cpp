#include "die_result.h"

#include <math.h>

namespace {

constexpr Vec3 TOWARD_GLASS{0, 0, 1};

} // namespace

UpFace upFace(const DieShape& shape, const Quaternion& orientation) {
    // Seeded from the first face rather than a sentinel below any alignment: every shape has faces
    // (die_shape.cpp), and a sentinel that survived the loop would reach acosf out of its domain.
    UpFace best{0, 0};
    float bestAlignment = dot(rotate(orientation, shape.faces[0].normal), TOWARD_GLASS);
    for (uint8_t i = 1; i < shape.faceCount; i++) {
        const float alignment = dot(rotate(orientation, shape.faces[i].normal), TOWARD_GLASS);
        if (alignment > bestAlignment) {
            bestAlignment = alignment;
            best.face = i;
        }
    }
    // Rounding can put a unit vector's dot product a hair past 1.
    best.tiltRad = acosf(fminf(1.0f, bestAlignment));
    return best;
}

DieResult startRoll() { return {DieState::Rolling, 0, 0}; }

DieResult updateResult(DieResult result, const DieBody& body, const DieShape& shape, const RestThresholds& thresholds) {
    if (result.state == DieState::Locked) return result;

    const bool still = length(body.velocity) < thresholds.linearMmPerS &&
                       length(body.angularVelocity) < thresholds.angularRadPerS;
    if (!still) return startRoll();

    result.stillS += PHYSICS_STEP_S;
    // The hold is the die's own, not the thresholds' (ADR-0028).
    if (result.stillS < shape.holdS) return result;

    const UpFace top = upFace(shape, body.orientation);
    if (top.tiltRad > thresholds.cockedTiltRad) {
        result.state = DieState::Cocked;
        return result;
    }
    result.state = DieState::Locked;
    result.numeral = shape.faces[top.face].numeral;
    return result;
}

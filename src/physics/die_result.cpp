#include "die_result.h"

#include <math.h>
#include <stdlib.h>

namespace {

constexpr Vec3 TOWARD_GLASS{0, 0, 1};
constexpr Vec3 TOWARD_FLOOR{0, 0, -1};

/**
 * @brief The face whose outward normal points most nearly along @p direction, and its angle from it.
 *
 * One function for both reading rules rather than the same loop written twice with one sign
 * changed, which is a difference a reader does not see (ADR-0033). The direction is named at
 * every call site for the same reason.
 */
UpFace faceAlong(const DieShape& shape, const Quaternion& orientation, Vec3 direction) {
    // Seeded from the first face rather than a sentinel below any alignment: every shape has faces
    // (die_shape.cpp), and a sentinel that survived the loop would reach acosf out of its domain.
    UpFace best{0, 0};
    float bestAlignment = dot(rotate(orientation, shape.faces[0].normal), direction);
    for (uint8_t i = 1; i < shape.faceCount; i++) {
        const float alignment = dot(rotate(orientation, shape.faces[i].normal), direction);
        if (alignment > bestAlignment) {
            bestAlignment = alignment;
            best.face = i;
        }
    }
    // Rounding can put a unit vector's dot product a hair past 1.
    best.tiltRad = acosf(fminf(1.0f, bestAlignment));
    return best;
}

} // namespace

UpFace upFace(const DieShape& shape, const Quaternion& orientation) {
    return faceAlong(shape, orientation, TOWARD_GLASS);
}

UpFace restingFace(const DieShape& shape, const Quaternion& orientation) {
    return faceAlong(shape, orientation, TOWARD_FLOOR);
}

UpFace readFace(const DieShape& shape, const Quaternion& orientation) {
    switch (shape.reading) {
        case NumeralReading::Apex:
            return restingFace(shape, orientation);
        case NumeralReading::TopFace:
            return upFace(shape, orientation);
    }
    // Outside the enum: a corrupted shape, not a die to guess at. Reading it as a top-face die
    // would be the quiet wrong answer, since that is what a zeroed DieShape looks like.
    abort();
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

    // Where the result is read is the die's own (ADR-0033); how flat it must lie to be read at
    // all is the product's, and is the same angle either way.
    const UpFace read = readFace(shape, body.orientation);
    if (read.tiltRad > thresholds.cockedTiltRad) {
        result.state = DieState::Cocked;
        return result;
    }
    result.state = DieState::Locked;
    result.numeral = shape.faces[read.face].numeral;
    return result;
}

#include "reorientation.h"

#include <math.h>
#include <stdlib.h>

namespace {

// Below this, a turn's vector part is too short to give an axis; the turn is then no turn at all.
constexpr float SMALLEST_TURN_SINE = 1e-6f;

} // namespace

Reorientation startReorientation(const DieBody& body, float durationS, UniformSource& source) {
    Quaternion turn = uniformOrientation(source);
    // q and −q are the same rotation; the one with w ≥ 0 is the shorter way round, at most half a turn.
    if (turn.w < 0) turn = {-turn.w, -turn.x, -turn.y, -turn.z};
    const Vec3 vector{turn.x, turn.y, turn.z};
    const float sine = length(vector);
    // One draw per statement, so the same seed draws the same way on every compiler.
    const Vec3 spinAxis = uniformDirection(source);
    const float spin = SPIN_MIN_RAD_PER_S + (SPIN_MAX_RAD_PER_S - SPIN_MIN_RAD_PER_S) * source.nextUnit();
    const uint32_t stepCount = stepsIn(durationS);
    if (stepCount == 0) abort();
    Reorientation reorientation{body.orientation, {1, 0, 0}, 0, 0, stepCount, spinAxis * spin, true};
    if (sine < SMALLEST_TURN_SINE) return reorientation;
    reorientation.bodyAxis = vector * (1 / sine);
    reorientation.angleRad = 2 * atan2f(sine, turn.w);
    return reorientation;
}

void continueReorientation(Reorientation& turn, DieBody& body) {
    if (!turn.active) return;
    const float share = static_cast<float>(turn.stepsTaken) / turn.stepCount;
    // Where the turn has reached, overriding what contacts did to it, so it ends exactly where it
    // was drawn to. Set before the step, with the spin, the step's contacts see the die turning
    // and lift it over its edge; set after, it would turn through the floor and be shoved back out.
    body.orientation = normalized(turn.start * quaternionAbout(turn.bodyAxis, share * turn.angleRad));
    if (turn.stepsTaken == turn.stepCount) {
        body.angularVelocity = turn.finalAngularVelocity;
        turn.active = false;
        return;
    }
    // A turn about one body axis keeps that axis fixed in the tray, so the spin is constant.
    body.angularVelocity = rotate(turn.start, turn.bodyAxis) * (turn.angleRad / (turn.stepCount * PHYSICS_STEP_S));
    turn.stepsTaken++;
}

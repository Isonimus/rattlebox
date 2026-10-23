#include "launch.h"

#include <math.h>
#include <stdlib.h>

namespace {

constexpr float TWO_PI = 6.2831853f;

float between(UniformSource& source, float low, float high) { return low + (high - low) * source.nextUnit(); }

} // namespace

Quaternion uniformOrientation(UniformSource& source) {
    const float u1 = source.nextUnit();
    const float angleA = TWO_PI * source.nextUnit();
    const float angleB = TWO_PI * source.nextUnit();
    const float a = sqrtf(1 - u1);
    const float b = sqrtf(u1);
    return {b * cosf(angleB), a * sinf(angleA), a * cosf(angleA), b * sinf(angleB)};
}

Vec3 uniformDirection(UniformSource& source) {
    // Archimedes: z uniform in [−1, 1] and the longitude uniform give a uniform point on the sphere.
    const float z = between(source, -1, 1);
    const float longitude = TWO_PI * source.nextUnit();
    const float ring = sqrtf(1 - z * z);
    return {ring * cosf(longitude), ring * sinf(longitude), z};
}

void launchDice(World& world, const Tray& tray, uint8_t dieCount, UniformSource& source) {
    // A launch that cannot fit is a programming error, not a roll to quietly shorten.
    if (dieCount == 0 || dieCount > MAX_DICE) abort();
    world.dieCount = dieCount;
    // The last roll's warm-start impulses belong to contacts that no longer exist: kept, they would
    // push the new launch, so the same seed would roll differently after a used World.
    world.memory = {};
    const float radius = world.shape->circumradius;
    // Each die starts in its own strip across the tray, inset by its bounding radius, so no two
    // start overlapping and none starts through a wall, with no retry loop.
    const float stripWidth = tray.widthMm / dieCount;
    if (stripWidth < 2 * radius || tray.depthMm < 2 * radius) abort();
    for (uint8_t die = 0; die < dieCount; die++) {
        DieBody& body = world.dice[die];
        const float stripStart = stripWidth * die;
        body.position = {between(source, stripStart + radius, stripStart + stripWidth - radius),
                         between(source, radius, tray.depthMm - radius), radius + DROP_HEIGHT_MM};
        body.orientation = uniformOrientation(source);
        // One draw per statement: the two operands of an expression are evaluated in no set
        // order, and a different order would make the same seed roll differently per compiler.
        const Vec3 throwDirection = uniformDirection(source);
        const float throwSpeed = between(source, THROW_MIN_MM_PER_S, THROW_MAX_MM_PER_S);
        const Vec3 spinAxis = uniformDirection(source);
        const float spin = between(source, SPIN_MIN_RAD_PER_S, SPIN_MAX_RAD_PER_S);
        body.velocity = throwDirection * throwSpeed;
        body.angularVelocity = spinAxis * spin;
    }
}

void nudgeCockedDie(DieBody& body, UniformSource& source) {
    const Vec3 direction = uniformDirection(source);
    // Upward, so the hop lifts the die off what it leans on rather than into the floor.
    const Vec3 upward{direction.x, direction.y, fabsf(direction.z)};
    body.velocity = body.velocity + upward * NUDGE_MM_PER_S;
    body.angularVelocity = body.angularVelocity + uniformDirection(source) * NUDGE_SPIN_RAD_PER_S;
}

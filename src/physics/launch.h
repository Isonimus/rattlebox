#ifndef LAUNCH_H
#define LAUNCH_H

#include "../geometry/quaternion.h"
#include "world.h"

/**
 * @brief Where a launch's randomness comes from: uniform floats in [0, 1).
 *
 * The device draws from the hardware RNG with an entropy source on (ADR-0002); the host's
 * fairness test draws from a seeded generator, so a run can be repeated exactly.
 */
class UniformSource {
public:
    virtual ~UniformSource() = default;
    virtual float nextUnit() = 0;
};

/** @brief An orientation drawn uniformly from all rotations (Shoemake's method). */
Quaternion uniformOrientation(UniformSource& source);

/** @brief A direction drawn uniformly from the unit sphere. */
Vec3 uniformDirection(UniformSource& source);

/**
 * @brief Throw @p dieCount dice into @p world: each at a uniform orientation, spinning about a
 * random axis, thrown in a random direction, from a random start that keeps it
 * inside the tray and clear of the others.
 */
void launchDice(World& world, const Tray& tray, uint8_t dieCount, UniformSource& source);

/**
 * @brief Knock a die that came to rest cocked: a hop in a random upward direction and a spin
 * about a random axis, both from @p source, so it falls again and can land flat.
 *
 * Random, never aimed at a face, so no result is steered (ADR-0002, ADR-0009). It fires as soon as the die
 * is seen cocked rather than waiting for the player: a player tilting a cocked die could choose
 * which of the two or three faces it leans between comes up.
 */
void nudgeCockedDie(DieBody& body, UniformSource& source);

#endif // LAUNCH_H

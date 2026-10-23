#ifndef REORIENTATION_H
#define REORIENTATION_H

#include "../geometry/quaternion.h"
#include "launch.h"
#include "world.h"

/**
 * @brief How long the product's spin takes (ADR-0010). With a gentle shake ending right at
 * detection, a D6 came back on its previous face 17.6% of the time at 0.05 s (p 0.020 against the
 * 16.7% chance), 17.3% at 0.1 s (p 0.11), 15.8% at 0.2 s (p 0.022) and 15.1% at 0.4 s (p below
 * 0.001): the closest to chance of the four. Why longer spins fall below chance was not established.
 */
constexpr float REORIENTATION_S = 0.1f;

/**
 * @brief A die spun to a fresh random orientation over a set time, instead of jumping to it.
 *
 * A shake alone, or a throw, leaves a die near the face it rested on; only a fresh uniformly random
 * orientation removes the previous result's pull. Spinning there hides the jump: the die turns at
 * a constant rate about one axis, which reads as a tumble.
 */
struct Reorientation {
    Quaternion start;
    /** @brief The turn's axis in the die's own frame, unit length. */
    Vec3       bodyAxis;
    float      angleRad;
    /** @brief Counted in steps, not summed seconds: a float sum of 1/240 s can fall short of the end. */
    uint32_t   stepsTaken;
    uint32_t   stepCount;
    /**
     * @brief The spin the die is left with: drawn on its own, since one about the turn's axis would
     * carry the die on the way it was turning, away from its start, and make the next face depend on it.
     */
    Vec3       finalAngularVelocity;
    bool       active;
};

/** @brief Draw a uniformly random turn for @p body to make over @p durationS. */
Reorientation startReorientation(const DieBody& body, float durationS, UniformSource& source);

/**
 * @brief Before each step of the world, put @p body where the turn has reached and spin it at the
 * turn's rate, so the step's contacts meet a die turning. On the call after the last step, the die
 * is put exactly at the drawn orientation and left to the physics with a spin of its own.
 */
void continueReorientation(Reorientation& turn, DieBody& body);

#endif // REORIENTATION_H

#ifndef DIE_RESULT_H
#define DIE_RESULT_H

#include <stdint.h>
#include "../geometry/die_shape.h"
#include "physics_config.h"
#include "world.h"

/** @brief When a die counts as at rest, and how flat it must lie for its result to read. */
struct RestThresholds {
    float linearMmPerS;
    float angularRadPerS;
    /** @brief How long both speeds must stay below their thresholds. */
    float holdS;
    /** @brief The widest angle between the up face's normal and straight up at which the die still lies flat. */
    float cockedTiltRad;
};

/** @brief The thresholds the product locks results with, measured by the fairness probe (slice 0008). */
constexpr RestThresholds PRODUCT_REST_THRESHOLDS{REST_LINEAR_MM_PER_S, REST_ANGULAR_RAD_PER_S, REST_HOLD_S,
                                                 COCKED_TILT_RAD};

enum class DieState : uint8_t {
    /** @brief Moving, or not still for the hold time yet. */
    Rolling,
    /** @brief At rest, but propped on a wall or another die: no face reads, so nothing locks. The
     * caller nudges it (nudgeCockedDie) and starts its result over. */
    Cocked,
    /** @brief Came to rest flat; its result is final until the next roll (invariant #3). */
    Locked,
};

/** @brief One die's progress toward its result over a roll. */
struct DieResult {
    DieState state;
    /** @brief The locked face's numeral; meaningful only once Locked. */
    uint8_t  numeral;
    float    stillS;
};

/**
 * @brief The face pointing most nearly at the glass (the tray's +z), and its angle from it.
 *
 * The tray's +z, not against gravity: the camera looks through the glass, so the face toward it
 * is the one the player reads, however the tray is tilted.
 */
struct UpFace {
    uint8_t face;
    float   tiltRad;
};

UpFace upFace(const DieShape& shape, const Quaternion& orientation);

/** @brief The state at the start of a roll: rolling, nothing locked. Only this clears a lock. */
DieResult startRoll();

/**
 * @brief Advance @p result by one step of @p body's motion.
 *
 * A locked result is returned unchanged whatever the die does: tilting the tray or a later
 * collision moves the die, never its result (ADR-0002, invariant #3).
 */
DieResult updateResult(DieResult result, const DieBody& body, const DieShape& shape, const RestThresholds& thresholds);

#endif // DIE_RESULT_H

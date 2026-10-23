#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>
#include "../geometry/die_shape.h"
#include "../geometry/quaternion.h"
#include "../geometry/vec3.h"
#include "physics_config.h"

/** @brief One die's rigid body: its centre's position and velocity, and its orientation and spin. */
struct DieBody {
    Vec3       position;
    Vec3       velocity;
    Quaternion orientation;
    /** @brief Angular velocity in the tray's frame. */
    Vec3       angularVelocity;
};

/**
 * @brief The tray as a box: the floor at z = 0, the glass at z = height, walls at x = 0, x =
 * width, y = 0 and y = depth. The camera looks down −z, so a result face points at +z.
 */
struct Tray {
    float widthMm;
    float depthMm;
    float heightMm;
};

constexpr Tray PRODUCT_TRAY{TRAY_WIDTH_MM, TRAY_DEPTH_MM, TRAY_HEIGHT_MM};

constexpr uint8_t TRAY_PLANE_COUNT = 6;

/** @brief The impulse a contact ended a step with. */
struct ContactImpulse {
    float normal;
    Vec3  tangent;
};

/**
 * @brief Each possible contact's impulse from the last step, so a contact that persists starts
 * the next step from it (warm starting). Without it the solver restarts from zero every step,
 * and 10 iterations left a D20 resting on a face drifting at 0.06 mm/s until it lost contact and
 * rocked for good; 40 cold iterations held it still, at four times the cost (ADR-0009).
 */
struct ContactMemory {
    ContactImpulse tray[MAX_DICE][DIE_MAX_VERTICES][TRAY_PLANE_COUNT];
    ContactImpulse dice[MAX_DICE][MAX_DICE];
};

/** @brief The dice in the tray: one shape, up to MAX_DICE bodies, and the gravity they fall by. */
struct World {
    const DieShape* shape;
    DieBody         dice[MAX_DICE];
    uint8_t         dieCount;
    /** @brief Gravity in the tray's frame: straight down on a level tray, sideways as it tilts. */
    Vec3            gravity;
    ContactMemory   memory;
};

/**
 * @brief Advance the world by one PHYSICS_STEP_S: gravity, then contacts between each die's
 * vertices and the tray's planes and between dice as bounding spheres (ADR-0002), solved by
 * sequential impulses, then the move, then a position correction for what still overlaps.
 */
void stepWorld(World& world, const Tray& tray);

/** @brief A die vertex's position in the tray's frame. */
Vec3 worldVertex(const DieBody& body, const DieShape& shape, uint8_t vertex);

/** @brief Kinetic plus gravitational energy, the floor as zero: what no step may raise. */
float totalEnergy(const World& world);

#endif // WORLD_H

#include <math.h>
#include <unity.h>
#include "../../src/geometry/die_shape.h"
#include "../../src/geometry/die_shape.cpp"
#include "../../src/geometry/quaternion.h"
#include "../../src/geometry/quaternion.cpp"
#include "../../src/geometry/rotation.h"
#include "../../src/geometry/rotation.cpp"
#include "../../src/physics/launch.h"
#include "../../src/physics/launch.cpp"
#include "../../src/physics/world.h"
#include "../../src/physics/world.cpp"
#include "../../src/probes/fairness/seeded_source.h"

static const float TOLERANCE = 1e-4f;
// How far a vertex may end up past a plane: the slop the position correction leaves on purpose,
// and a little rounding.
static const float PENETRATION_TOLERANCE_MM = PENETRATION_SLOP_MM + 0.005f;
// Float rounding on energies of about 10⁵ (mm²/s² at unit mass) for a die in the air.
static const float ENERGY_ROUNDING = 1.0f;
static const uint32_t TWO_SECONDS = stepsIn(2.0f);
static const uint32_t THREE_SECONDS = stepsIn(3.0f);
static const uint32_t THROWS = 40;
static const uint64_t SEED = 1;
// Steep enough that a D6 slides on the tray's friction of 0.35 (tan 30° = 0.58).
static const float TRAY_TILT_DEG = 30.0f;
static const float HEAD_ON_MM_PER_S = 600.0f;
// A 0.1 mm drop lands at about 44 mm/s, so a bounce would leave at about 29 mm/s at the tray's
// restitution; a die that does not bounce leaves the floor at no speed at all.
static const float NO_BOUNCE_MM_PER_S = 1.0f;

static void assertVecEqual(Vec3 expected, Vec3 actual, float tolerance) {
    TEST_ASSERT_FLOAT_WITHIN(tolerance, expected.x, actual.x);
    TEST_ASSERT_FLOAT_WITHIN(tolerance, expected.y, actual.y);
    TEST_ASSERT_FLOAT_WITHIN(tolerance, expected.z, actual.z);
}

static World levelWorld(const DieShape& shape) {
    World world{};
    world.shape = &shape;
    world.gravity = {0, 0, -GRAVITY_MM_PER_S2};
    return world;
}

/** @brief The orientation that turns @p face of @p shape straight down, onto the floor. */
static Quaternion faceDown(const DieShape& shape, uint8_t face) {
    const Vec3 normal = shape.faces[face].normal;
    const Vec3 down{0, 0, -1};
    const Vec3 axis = cross(normal, down);
    if (length(axis) < TOLERANCE) return dot(normal, down) > 0 ? IDENTITY_QUATERNION : quaternionAbout({1, 0, 0}, M_PI);
    return quaternionAbout(axis, acosf(dot(normal, down)));
}

/** @brief A die lying still on @p face at (@p x, @p y), touching the floor. */
static DieBody restingOn(const DieShape& shape, uint8_t face, float x, float y) {
    const Quaternion orientation = faceDown(shape, face);
    float lowest = 0;
    for (uint8_t i = 0; i < shape.vertexCount; i++) lowest = fminf(lowest, rotate(orientation, shape.vertices[i]).z);
    return {{x, y, -lowest}, {0, 0, 0}, orientation, {0, 0, 0}};
}

/**
 * @brief The most a step may raise the energy by integrating in steps. Gravity's kinetic energy is
 * added with the velocity before the contacts act and its potential with the velocity after, so a
 * contact that turns a die round gains at most m·dt·|g·Δv| + ½·m·g²·dt², Δv being what the contacts
 * changed. Anything the solver or the position correction adds beyond it is a pump: before
 * speculative contacts, the correction kept a landed D20 rocking for good.
 */
static float steppingAllowance(const World& before, const World& after) {
    const float gravitySquared = dot(before.gravity, before.gravity);
    float allowance = 0;
    for (uint8_t die = 0; die < before.dieCount; die++) {
        const Vec3 beforeContacts = before.dice[die].velocity + before.gravity * PHYSICS_STEP_S;
        const Vec3 contactChange = after.dice[die].velocity - beforeContacts;
        allowance += DIE_MASS * PHYSICS_STEP_S * fabsf(dot(before.gravity, contactChange)) +
                     DIE_MASS * gravitySquared * PHYSICS_STEP_S * PHYSICS_STEP_S / 2;
    }
    return allowance;
}

static void assertInsideTheTray(const World& world) {
    for (uint8_t die = 0; die < world.dieCount; die++) {
        for (uint8_t vertex = 0; vertex < world.shape->vertexCount; vertex++) {
            const Vec3 p = worldVertex(world.dice[die], *world.shape, vertex);
            TEST_ASSERT_TRUE_MESSAGE(p.z >= -PENETRATION_TOLERANCE_MM, "a vertex sank through the floor");
            TEST_ASSERT_TRUE_MESSAGE(p.z <= PRODUCT_TRAY.heightMm + PENETRATION_TOLERANCE_MM, "a vertex left through the glass");
            TEST_ASSERT_TRUE_MESSAGE(p.x >= -PENETRATION_TOLERANCE_MM, "a vertex left through a wall");
            TEST_ASSERT_TRUE_MESSAGE(p.x <= PRODUCT_TRAY.widthMm + PENETRATION_TOLERANCE_MM, "a vertex left through a wall");
            TEST_ASSERT_TRUE_MESSAGE(p.y >= -PENETRATION_TOLERANCE_MM, "a vertex left through a wall");
            TEST_ASSERT_TRUE_MESSAGE(p.y <= PRODUCT_TRAY.depthMm + PENETRATION_TOLERANCE_MM, "a vertex left through a wall");
        }
    }
}

// ---- quaternion

void test_a_quaternion_rotates_as_its_matrix_does() {
    const Quaternion q = quaternionAbout({1, 2, 3}, 0.7f);
    const Rotation matrix = toRotation(q);
    const Vec3 v{2, -1, 4};
    assertVecEqual(rotate(matrix, v), rotate(q, v), TOLERANCE);
    assertVecEqual(rotate(rotationAbout({1, 2, 3}, 0.7f), v), rotate(q, v), TOLERANCE);
}

void test_integrating_a_steady_spin_turns_by_its_angle() {
    // A quarter turn a second about z, for one second of steps, takes x to y.
    const Vec3 omega{0, 0, static_cast<float>(M_PI / 2)};
    Quaternion q = IDENTITY_QUATERNION;
    for (uint32_t step = 0; step < stepsIn(1.0f); step++) q = integrate(q, omega, PHYSICS_STEP_S);
    assertVecEqual({0, 1, 0}, rotate(q, {1, 0, 0}), 1e-3f);
}

// ---- resting

static void assertADieRestingFlatStaysPut(const DieShape& shape) {
    World world = levelWorld(shape);
    world.dieCount = 1;
    world.dice[0] = restingOn(shape, 0, PRODUCT_TRAY.widthMm / 2, PRODUCT_TRAY.depthMm / 2);
    const Vec3 start = world.dice[0].position;
    for (uint32_t step = 0; step < TWO_SECONDS; step++) stepWorld(world, PRODUCT_TRAY);
    assertVecEqual(start, world.dice[0].position, PENETRATION_TOLERANCE_MM);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0, length(world.dice[0].velocity));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0, length(world.dice[0].angularVelocity));
}

void test_a_d6_resting_flat_stays_put() { assertADieRestingFlatStaysPut(makeD6(D6_EDGE_MM)); }

void test_a_d20_resting_flat_stays_put() {
    // Before warm starting, this die drifted up at 0.06 mm/s until it lost contact and rocked.
    assertADieRestingFlatStaysPut(makeD20(D20_EDGE_MM));
}

// ---- thrown dice

/** @brief Throw @p dieCount dice THROWS times and step each throw for 3 s, checking every step. */
static void throwAndWatch(const DieShape& shape, uint8_t dieCount) {
    SeededSource source(SEED);
    for (uint32_t i = 0; i < THROWS; i++) {
        World world = levelWorld(shape);
        launchDice(world, PRODUCT_TRAY, dieCount, source);
        for (uint32_t step = 0; step < THREE_SECONDS; step++) {
            const World before = world;
            stepWorld(world, PRODUCT_TRAY);
            assertInsideTheTray(world);
            const float rise = totalEnergy(world) - totalEnergy(before);
            TEST_ASSERT_TRUE_MESSAGE(rise <= steppingAllowance(before, world) + ENERGY_ROUNDING,
                                     "a step raised the energy beyond what stepping in time explains");
        }
        // Every throw comes to rest: nothing keeps buzzing.
        for (uint8_t die = 0; die < dieCount; die++) {
            TEST_ASSERT_TRUE(length(world.dice[die].velocity) < REST_LINEAR_MM_PER_S);
            TEST_ASSERT_TRUE(length(world.dice[die].angularVelocity) < REST_ANGULAR_RAD_PER_S);
        }
    }
}

void test_thrown_d6s_stay_in_the_tray_lose_energy_and_come_to_rest() { throwAndWatch(makeD6(D6_EDGE_MM), MAX_DICE); }

void test_thrown_d20s_stay_in_the_tray_lose_energy_and_come_to_rest() {
    // Before speculative contacts, a landed D20 kept rocking: pushing a vertex back out after it
    // passed through the floor lifted the die without slowing it.
    throwAndWatch(makeD20(D20_EDGE_MM), MAX_DICE);
}

void test_a_die_resting_on_the_floor_does_not_bounce() {
    // A contact closing slower than the bounce threshold does not bounce; with the threshold
    // under 2 g·dt / (1 − e), a D20 hopped in place for good.
    const DieShape shape = makeD20(D20_EDGE_MM);
    World world = levelWorld(shape);
    world.dieCount = 1;
    world.dice[0] = restingOn(shape, 3, PRODUCT_TRAY.widthMm / 2, PRODUCT_TRAY.depthMm / 2);
    world.dice[0].position.z += 0.1f;
    for (uint32_t step = 0; step < TWO_SECONDS; step++) {
        stepWorld(world, PRODUCT_TRAY);
        TEST_ASSERT_TRUE_MESSAGE(world.dice[0].velocity.z < NO_BOUNCE_MM_PER_S, "a die dropped 0.1 mm bounced");
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0, length(world.dice[0].velocity));
}

void test_two_dice_that_meet_push_apart() {
    const DieShape shape = makeD20(D20_EDGE_MM);
    World world = levelWorld(shape);
    world.dieCount = 2;
    const float y = PRODUCT_TRAY.depthMm / 2;
    world.dice[0] = restingOn(shape, 0, PRODUCT_TRAY.widthMm / 4, y);
    world.dice[1] = restingOn(shape, 0, PRODUCT_TRAY.widthMm * 3 / 4, y);
    world.dice[0].velocity = {HEAD_ON_MM_PER_S, 0, 0};
    world.dice[1].velocity = {-HEAD_ON_MM_PER_S, 0, 0};
    float closest = PRODUCT_TRAY.widthMm;
    for (uint32_t step = 0; step < TWO_SECONDS; step++) {
        stepWorld(world, PRODUCT_TRAY);
        closest = fminf(closest, length(world.dice[0].position - world.dice[1].position));
    }
    // They met: a speculative contact bounces up to one step's travel early, so the spheres came
    // within that of touching, and the dice ended apart in their order.
    const float closingPerStepMm = 2 * HEAD_ON_MM_PER_S * PHYSICS_STEP_S;
    TEST_ASSERT_TRUE(closest <= 2 * shape.circumradius + closingPerStepMm);
    TEST_ASSERT_TRUE(world.dice[0].position.x < world.dice[1].position.x);
    TEST_ASSERT_TRUE(length(world.dice[0].position - world.dice[1].position) >= 2 * shape.circumradius - PENETRATION_TOLERANCE_MM);
}

void test_sideways_gravity_slides_a_die_against_the_wall() {
    // A tilted tray is gravity with a sideways part: the die ends against the low wall.
    const DieShape shape = makeD6(D6_EDGE_MM);
    World world = levelWorld(shape);
    const float tilt = TRAY_TILT_DEG * RAD_PER_DEG;
    world.gravity = {-GRAVITY_MM_PER_S2 * sinf(tilt), 0, -GRAVITY_MM_PER_S2 * cosf(tilt)};
    world.dieCount = 1;
    world.dice[0] = restingOn(shape, 0, PRODUCT_TRAY.widthMm / 2, PRODUCT_TRAY.depthMm / 2);
    for (uint32_t step = 0; step < TWO_SECONDS; step++) stepWorld(world, PRODUCT_TRAY);
    TEST_ASSERT_FLOAT_WITHIN(PENETRATION_TOLERANCE_MM, D6_EDGE_MM / 2, world.dice[0].position.x);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_quaternion_rotates_as_its_matrix_does);
    RUN_TEST(test_integrating_a_steady_spin_turns_by_its_angle);
    RUN_TEST(test_a_d6_resting_flat_stays_put);
    RUN_TEST(test_a_d20_resting_flat_stays_put);
    RUN_TEST(test_thrown_d6s_stay_in_the_tray_lose_energy_and_come_to_rest);
    RUN_TEST(test_thrown_d20s_stay_in_the_tray_lose_energy_and_come_to_rest);
    RUN_TEST(test_a_die_resting_on_the_floor_does_not_bounce);
    RUN_TEST(test_two_dice_that_meet_push_apart);
    RUN_TEST(test_sideways_gravity_slides_a_die_against_the_wall);
    return UNITY_END();
}

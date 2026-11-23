#include <math.h>
#include <unity.h>
#include "../../src/geometry/die_shape.h"
#include "../../src/geometry/die_shape.cpp"
#include "../../src/geometry/quaternion.h"
#include "../../src/geometry/quaternion.cpp"
#include "../../src/geometry/rotation.h"
#include "../../src/geometry/rotation.cpp"
#include "../../src/physics/die_result.h"
#include "../../src/physics/die_result.cpp"
#include "../../src/physics/launch.h"
#include "../../src/physics/launch.cpp"
#include "../../src/physics/world.h"
#include "../../src/physics/world.cpp"
#include "../../src/probes/fairness/seeded_source.h"
#include "../../src/roll/dice_roll.h"
#include "../../src/roll/dice_roll.cpp"

// How a roll advances, step by step, on the device and in the fairness test alike (slice 0011).

static const uint64_t SEED = 1;
static const RestThresholds THRESHOLDS = PRODUCT_REST_THRESHOLDS;
static const uint8_t TOP_FACE = 7;
// A lean well past the cocked tolerance, as a die propped on a wall measured at 1° to 28°.
static const float PROPPED_TILT_RAD = 10 * RAD_PER_DEG;
// Mid-tray, resting on the floor: far from every wall, so a step leaves it where it is.
static const Vec3 MID_TRAY_MM{TRAY_WIDTH_MM / 2, TRAY_DEPTH_MM / 2, 0};

static const DieShape D20 = makeD20(D20_EDGE_MM);

/**
 * @brief Steps of stillness before a die of this hold locks, counted the way the product counts:
 * it adds a step to stillS and compares, so a hold that is not a whole number of steps takes the
 * step that passes it. Rounding the hold to the nearest step instead can land just under it.
 */
static uint32_t stepsToHold(float holdS) {
    uint32_t steps = 0;
    for (float stillS = 0; stillS < holdS; stillS += PHYSICS_STEP_S) steps++;
    return steps;
}

// The hold is the die's own now, not one the thresholds carry for every die (ADR-0028).
static const uint32_t HOLD_STEPS = stepsToHold(D20.holdS);

static Quaternion faceUp(const DieShape& shape, uint8_t face) {
    const Vec3 normal = shape.faces[face].normal;
    return quaternionAbout(cross(normal, {0, 0, 1}), acosf(normal.z));
}

/** @brief A world of one D20 held still at @p orientation, not touching anything. */
static World stillWorld(Quaternion orientation) {
    World world{};
    world.shape = &D20;
    world.dieCount = 1;
    world.dice[0] = {MID_TRAY_MM, {0, 0, 0}, orientation, {0, 0, 0}};
    return world;
}

void test_a_launch_starts_every_result_over_on_a_level_tray() {
    SeededSource source(SEED);
    World world{};
    world.shape = &D20;
    world.gravity = {GRAVITY_MM_PER_S2, 0, 0};
    DieResult results[MAX_DICE];
    for (DieResult& result : results) result = {DieState::Locked, 1, 0};
    launchRoll(world, results, MAX_DICE, source);
    TEST_ASSERT_EQUAL_UINT8(MAX_DICE, world.dieCount);
    TEST_ASSERT_EQUAL_FLOAT(-GRAVITY_MM_PER_S2, world.gravity.z);
    TEST_ASSERT_EQUAL_FLOAT(0, world.gravity.x);
    for (const DieResult& result : results) TEST_ASSERT_EQUAL(DieState::Rolling, result.state);
}

void test_a_step_moves_the_dice() {
    SeededSource source(SEED);
    World world{};
    world.shape = &D20;
    DieResult results[MAX_DICE];
    launchRoll(world, results, 1, source);
    const Vec3 before = world.dice[0].position;
    stepRoll(world, results, source, THRESHOLDS);
    TEST_ASSERT_TRUE(length(world.dice[0].position - before) > 0);
}

void test_a_die_resting_flat_locks_to_the_face_it_shows() {
    SeededSource source(SEED);
    World world = stillWorld(faceUp(D20, TOP_FACE));
    DieResult results[] = {startRoll()};
    for (uint32_t step = 0; step < HOLD_STEPS; step++) advanceResults(world, results, source, THRESHOLDS);
    TEST_ASSERT_TRUE(everyDieLocked(results, 1));
    TEST_ASSERT_EQUAL_UINT8(D20.faces[TOP_FACE].numeral, results[0].numeral);
    TEST_ASSERT_EQUAL_UINT8(D20.faces[TOP_FACE].numeral, shownNumeral(world, 0));
}

void test_a_die_resting_cocked_is_nudged_and_starts_over() {
    SeededSource source(SEED);
    const Quaternion leaning = quaternionAbout({1, 0, 0}, PROPPED_TILT_RAD) * faceUp(D20, TOP_FACE);
    World world = stillWorld(leaning);
    DieResult results[] = {startRoll()};
    uint8_t nudges = 0;
    for (uint32_t step = 0; step < HOLD_STEPS; step++) nudges += advanceResults(world, results, source, THRESHOLDS);
    TEST_ASSERT_EQUAL_UINT8(1, nudges);
    TEST_ASSERT_EQUAL(DieState::Rolling, results[0].state);
    TEST_ASSERT_TRUE(world.dice[0].velocity.z > 0);
}

void test_the_roll_is_settled_only_once_every_die_has_locked() {
    const DieResult locked{DieState::Locked, 4, 0};
    const DieResult rolling = startRoll();
    const DieResult oneStill[] = {locked, locked, rolling};
    const DieResult allLocked[] = {locked, locked, locked};
    TEST_ASSERT_FALSE(everyDieLocked(oneStill, MAX_DICE));
    TEST_ASSERT_TRUE(everyDieLocked(oneStill, 2));
    TEST_ASSERT_TRUE(everyDieLocked(allLocked, MAX_DICE));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_launch_starts_every_result_over_on_a_level_tray);
    RUN_TEST(test_a_step_moves_the_dice);
    RUN_TEST(test_a_die_resting_flat_locks_to_the_face_it_shows);
    RUN_TEST(test_a_die_resting_cocked_is_nudged_and_starts_over);
    RUN_TEST(test_the_roll_is_settled_only_once_every_die_has_locked);
    return UNITY_END();
}

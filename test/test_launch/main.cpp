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

static const uint64_t SEED = 1;
static const uint32_t LAUNCHES = 500;
// The face toward the glass at launch, over this many orientations, is tested the way invariant
// #2 tests results: chi-square at α = 0.001 against uniform, 19 degrees of freedom for a D20.
static const uint32_t ORIENTATIONS = 20000;
static const double CHI_SQUARE_CRITICAL_D20 = 43.820;
static const float TOLERANCE = 1e-4f;
// A unit vector's components average 0 over a uniform sphere; over 20,000 draws the mean's
// standard error is 1 / sqrt(3 × 20,000) = 0.004, so 0.02 is five of them.
static const float MEAN_COMPONENT_TOLERANCE = 0.02f;

static const DieShape D20 = makeD20(D20_EDGE_MM);
static const DieShape D6 = makeD6(D6_EDGE_MM);

static World launched(const DieShape& shape, uint8_t dieCount, UniformSource& source) {
    World world{};
    world.shape = &shape;
    world.gravity = {0, 0, -GRAVITY_MM_PER_S2};
    launchDice(world, PRODUCT_TRAY, dieCount, source);
    return world;
}

void test_the_same_seed_gives_the_same_launches() {
    SeededSource first(SEED);
    SeededSource second(SEED);
    for (uint32_t i = 0; i < LAUNCHES; i++) {
        const World a = launched(D20, MAX_DICE, first);
        const World b = launched(D20, MAX_DICE, second);
        for (uint8_t die = 0; die < MAX_DICE; die++) {
            TEST_ASSERT_EQUAL_MEMORY(&a.dice[die], &b.dice[die], sizeof(DieBody));
        }
    }
}

void test_a_relaunch_into_a_used_world_matches_a_fresh_one() {
    // The device reuses one World across rolls; nothing of the last roll, its warm-start impulses
    // included, may change how the next one goes.
    SeededSource settle(SEED);
    World used = launched(D20, MAX_DICE, settle);
    for (uint32_t step = 0; step < stepsIn(3.0f); step++) stepWorld(used, PRODUCT_TRAY);
    SeededSource first(SEED + 1);
    SeededSource second(SEED + 1);
    launchDice(used, PRODUCT_TRAY, MAX_DICE, first);
    World fresh = launched(D20, MAX_DICE, second);
    for (uint32_t step = 0; step < stepsIn(0.5f); step++) {
        stepWorld(used, PRODUCT_TRAY);
        stepWorld(fresh, PRODUCT_TRAY);
    }
    for (uint8_t die = 0; die < MAX_DICE; die++) TEST_ASSERT_EQUAL_MEMORY(&fresh.dice[die], &used.dice[die], sizeof(DieBody));
}

void test_seeds_give_different_launches() {
    SeededSource first(SEED);
    SeededSource second(SEED + 1);
    const World a = launched(D20, 1, first);
    const World b = launched(D20, 1, second);
    TEST_ASSERT_FALSE(a.dice[0].position.x == b.dice[0].position.x && a.dice[0].velocity.x == b.dice[0].velocity.x);
}

static void assertLaunchesStartInsideApartAndInRange(const DieShape& shape, uint8_t dieCount) {
    SeededSource source(SEED);
    for (uint32_t i = 0; i < LAUNCHES; i++) {
        const World world = launched(shape, dieCount, source);
        TEST_ASSERT_EQUAL_UINT8(dieCount, world.dieCount);
        for (uint8_t die = 0; die < dieCount; die++) {
            const DieBody& body = world.dice[die];
            for (uint8_t vertex = 0; vertex < shape.vertexCount; vertex++) {
                const Vec3 p = worldVertex(body, shape, vertex);
                TEST_ASSERT_TRUE(p.x >= 0 && p.x <= PRODUCT_TRAY.widthMm);
                TEST_ASSERT_TRUE(p.y >= 0 && p.y <= PRODUCT_TRAY.depthMm);
                TEST_ASSERT_TRUE(p.z >= DROP_HEIGHT_MM - TOLERANCE && p.z <= PRODUCT_TRAY.heightMm);
            }
            const float speed = length(body.velocity);
            TEST_ASSERT_TRUE(speed >= THROW_MIN_MM_PER_S - TOLERANCE && speed <= THROW_MAX_MM_PER_S + TOLERANCE);
            const float spin = length(body.angularVelocity);
            TEST_ASSERT_TRUE(spin >= SPIN_MIN_RAD_PER_S - TOLERANCE && spin <= SPIN_MAX_RAD_PER_S + TOLERANCE);
            for (uint8_t other = die + 1; other < dieCount; other++) {
                TEST_ASSERT_TRUE(length(body.position - world.dice[other].position) >= 2 * shape.circumradius);
            }
        }
    }
}

void test_one_d20_starts_inside_the_tray_and_in_range() { assertLaunchesStartInsideApartAndInRange(D20, 1); }
void test_three_d6s_start_inside_the_tray_apart_and_in_range() { assertLaunchesStartInsideApartAndInRange(D6, MAX_DICE); }

void test_orientations_are_uniform_by_the_face_they_turn_to_the_glass() {
    SeededSource source(SEED);
    uint32_t counts[DIE_MAX_FACES] = {};
    for (uint32_t i = 0; i < ORIENTATIONS; i++) counts[upFace(D20, uniformOrientation(source)).face]++;
    const double expected = static_cast<double>(ORIENTATIONS) / D20.faceCount;
    double statistic = 0;
    for (uint8_t face = 0; face < D20.faceCount; face++) statistic += (counts[face] - expected) * (counts[face] - expected) / expected;
    TEST_ASSERT_LESS_THAN_DOUBLE_MESSAGE(CHI_SQUARE_CRITICAL_D20, statistic, "launch orientations favour some faces");
}

void test_directions_are_unit_length_and_centred() {
    SeededSource source(SEED);
    Vec3 sum{0, 0, 0};
    for (uint32_t i = 0; i < ORIENTATIONS; i++) {
        const Vec3 direction = uniformDirection(source);
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, 1, length(direction));
        sum = sum + direction;
    }
    const Vec3 mean = sum * (1.0f / ORIENTATIONS);
    TEST_ASSERT_FLOAT_WITHIN(MEAN_COMPONENT_TOLERANCE, 0, mean.x);
    TEST_ASSERT_FLOAT_WITHIN(MEAN_COMPONENT_TOLERANCE, 0, mean.y);
    TEST_ASSERT_FLOAT_WITHIN(MEAN_COMPONENT_TOLERANCE, 0, mean.z);
}

void test_a_nudge_hops_the_die_upward_by_its_size() {
    SeededSource source(SEED);
    for (uint32_t i = 0; i < LAUNCHES; i++) {
        DieBody body{{50, 30, 10}, {0, 0, 0}, IDENTITY_QUATERNION, {0, 0, 0}};
        nudgeCockedDie(body, source);
        TEST_ASSERT_TRUE_MESSAGE(body.velocity.z >= 0, "a nudge pushed a die into the floor");
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE * NUDGE_MM_PER_S, NUDGE_MM_PER_S, length(body.velocity));
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE * NUDGE_SPIN_RAD_PER_S, NUDGE_SPIN_RAD_PER_S, length(body.angularVelocity));
    }
}

void test_seeded_values_stay_in_the_unit_interval() {
    SeededSource source(SEED);
    for (uint32_t i = 0; i < ORIENTATIONS; i++) {
        const float value = source.nextUnit();
        TEST_ASSERT_TRUE(value >= 0.0f && value < 1.0f);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_same_seed_gives_the_same_launches);
    RUN_TEST(test_a_relaunch_into_a_used_world_matches_a_fresh_one);
    RUN_TEST(test_seeds_give_different_launches);
    RUN_TEST(test_one_d20_starts_inside_the_tray_and_in_range);
    RUN_TEST(test_three_d6s_start_inside_the_tray_apart_and_in_range);
    RUN_TEST(test_orientations_are_uniform_by_the_face_they_turn_to_the_glass);
    RUN_TEST(test_directions_are_unit_length_and_centred);
    RUN_TEST(test_a_nudge_hops_the_die_upward_by_its_size);
    RUN_TEST(test_seeded_values_stay_in_the_unit_interval);
    return UNITY_END();
}

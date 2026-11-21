#include <math.h>
#include <unity.h>
#include "../../src/geometry/die_shape.h"
#include "../../src/geometry/die_shape.cpp"
#include "../../src/geometry/quaternion.h"
#include "../../src/geometry/quaternion.cpp"
#include "../../src/geometry/rotation.h"
#include "../../src/geometry/rotation.cpp"
#include "../../src/physics/world.h"
#include "../../src/physics/world.cpp"
#include "../../src/sound/hit_finder.h"
#include "../../src/sound/hit_finder.cpp"

// The hits the clacks are played from, found in the physics' own contact impulses (slice 0013).

static const uint32_t TWO_SECONDS = stepsIn(2.0f);
// High enough that the die lands at about 630 mm/s, well past the bounce threshold, so it bounces.
static const float DROP_MM = 20.0f;
static const float HEAD_ON_MM_PER_S = 600.0f;
// The solver's iterations stop short of the exact impulse, by well under this share.
static const float IMPULSE_TOLERANCE = 0.05f;

static const DieShape D6 = makeD6(D6_EDGE_MM);
static const DieShape D20 = makeD20(D20_EDGE_MM);

/** @brief A D6 lying still on its first face at (@p x, @p y), touching the floor of a level tray. */
static World restingD6(float x, float y) {
    World world{};
    world.shape = &D6;
    world.gravity = {0, 0, -GRAVITY_MM_PER_S2};
    world.dieCount = 1;
    // The D6's first face points along −z unturned, so its four lowest vertices are on the floor.
    float lowest = 0;
    for (uint8_t i = 0; i < D6.vertexCount; i++) lowest = fminf(lowest, D6.vertices[i].z);
    world.dice[0] = {{x, y, -lowest}, {0, 0, 0}, IDENTITY_QUATERNION, {0, 0, 0}};
    return world;
}

/** @brief Step @p world once and return how many hits the step made, the first into @p firstImpulse. */
static uint8_t stepAndFind(World& world, HitFinder& finder, float& firstImpulse) {
    stepWorld(world, PRODUCT_TRAY);
    float impulses[MAX_HITS_PER_STEP];
    const uint8_t count = finder.find(world, impulses);
    if (count > 0) firstImpulse = impulses[0];
    return count;
}

void test_a_die_landing_flat_is_one_hit_of_its_whole_impulse() {
    World world = restingD6(PRODUCT_TRAY.widthMm / 2, PRODUCT_TRAY.depthMm / 2);
    world.dice[0].position.z += DROP_MM;
    HitFinder finder;
    float impulse = 0;
    uint8_t hits = 0;
    float speed = 0;
    for (uint32_t step = 0; step < TWO_SECONDS && hits == 0; step++) {
        speed = -world.dice[0].velocity.z;
        hits = stepAndFind(world, finder, impulse);
    }
    // Four vertices land together; as four hits, one landing would clack four times over.
    TEST_ASSERT_EQUAL_UINT8(1, hits);
    // Stopped and sent back up at the tray's restitution, and held against one step of gravity.
    const float expected = DIE_MASS * ((1 + TRAY_RESTITUTION) * speed + GRAVITY_MM_PER_S2 * PHYSICS_STEP_S);
    TEST_ASSERT_FLOAT_WITHIN(IMPULSE_TOLERANCE * expected, expected, impulse);
}

void test_a_bouncing_die_hits_each_time_it_comes_down() {
    World world = restingD6(PRODUCT_TRAY.widthMm / 2, PRODUCT_TRAY.depthMm / 2);
    world.dice[0].position.z += DROP_MM;
    HitFinder finder;
    float impulse = 0;
    uint32_t hitSteps = 0;
    for (uint32_t step = 0; step < TWO_SECONDS; step++) hitSteps += stepAndFind(world, finder, impulse) > 0;
    TEST_ASSERT_TRUE(hitSteps >= 2);
}

void test_a_die_resting_on_the_floor_makes_no_hits() {
    World world = restingD6(PRODUCT_TRAY.widthMm / 2, PRODUCT_TRAY.depthMm / 2);
    HitFinder finder;
    float impulse = 0;
    // The first step finds the resting contacts new: the finder had seen no step before it.
    stepAndFind(world, finder, impulse);
    for (uint32_t step = 0; step < TWO_SECONDS; step++) TEST_ASSERT_EQUAL_UINT8(0, stepAndFind(world, finder, impulse));
}

void test_two_dice_meeting_head_on_are_one_hit() {
    // Weightless, so only the dice meeting can make a contact before they reach the walls.
    World world{};
    world.shape = &D20;
    world.gravity = {0, 0, 0};
    world.dieCount = 2;
    const Vec3 middle{PRODUCT_TRAY.widthMm / 2, PRODUCT_TRAY.depthMm / 2, PRODUCT_TRAY.heightMm / 2};
    const Vec3 apart{2 * D20.circumradius, 0, 0};
    world.dice[0] = {middle - apart, {HEAD_ON_MM_PER_S, 0, 0}, IDENTITY_QUATERNION, {0, 0, 0}};
    world.dice[1] = {middle + apart, {-HEAD_ON_MM_PER_S, 0, 0}, IDENTITY_QUATERNION, {0, 0, 0}};
    HitFinder finder;
    float impulse = 0;
    uint8_t hits = 0;
    for (uint32_t step = 0; step < TWO_SECONDS && hits == 0; step++) hits = stepAndFind(world, finder, impulse);
    TEST_ASSERT_EQUAL_UINT8(1, hits);
    // Each die turned back at the dice's restitution.
    const float expected = DIE_MASS * (1 + DIE_RESTITUTION) * HEAD_ON_MM_PER_S;
    TEST_ASSERT_FLOAT_WITHIN(IMPULSE_TOLERANCE * expected, expected, impulse);
}

void test_dice_pressed_together_are_not_hit_again() {
    // The tray on its side: gravity along −x stacks the second D20 on the first against the wall,
    // so their contact pushes every step once they settle.
    World world{};
    world.shape = &D20;
    world.gravity = {-GRAVITY_MM_PER_S2, 0, 0};
    world.dieCount = 2;
    const float y = PRODUCT_TRAY.depthMm / 2;
    const float z = PRODUCT_TRAY.heightMm / 2;
    world.dice[0] = {{D20.circumradius, y, z}, {0, 0, 0}, IDENTITY_QUATERNION, {0, 0, 0}};
    world.dice[1] = {{3 * D20.circumradius, y, z}, {0, 0, 0}, IDENTITY_QUATERNION, {0, 0, 0}};
    HitFinder finder;
    float impulse = 0;
    for (uint32_t step = 0; step < TWO_SECONDS; step++) stepAndFind(world, finder, impulse);
    TEST_ASSERT_TRUE(world.memory.dice[0][1].normal > 0);
    for (uint32_t step = 0; step < TWO_SECONDS; step++) TEST_ASSERT_EQUAL_UINT8(0, stepAndFind(world, finder, impulse));
}

/** @brief The hits @p finder finds in a step whose only push is @p pushing, at @p impulse. */
static uint8_t findWith(HitFinder& finder, World& world, ContactImpulse* pushing, float impulse) {
    world.memory = {};
    if (pushing != nullptr) pushing->normal = impulse;
    float impulses[MAX_HITS_PER_STEP];
    return finder.find(world, impulses);
}

void test_a_contact_that_ends_on_a_vertex_out_of_play_hits_again() {
    // A D20 vertex the D6 has none of: it stops pushing during the D6's roll, so when the D20 is
    // back, its next push is a new contact.
    const uint8_t beyondD6 = D20.vertexCount - 1;
    World world{};
    world.shape = &D20;
    world.dieCount = 1;
    HitFinder finder;
    TEST_ASSERT_EQUAL_UINT8(1, findWith(finder, world, &world.memory.tray[0][beyondD6][0], GRAVITY_MM_PER_S2));
    world.shape = &D6;
    TEST_ASSERT_EQUAL_UINT8(0, findWith(finder, world, nullptr, 0));
    world.shape = &D20;
    TEST_ASSERT_EQUAL_UINT8(1, findWith(finder, world, &world.memory.tray[0][beyondD6][0], GRAVITY_MM_PER_S2));
}

void test_a_contact_that_ends_on_a_die_out_of_play_hits_again() {
    const uint8_t lastDie = MAX_DICE - 1;
    World world{};
    world.shape = &D6;
    world.dieCount = MAX_DICE;
    HitFinder finder;
    TEST_ASSERT_EQUAL_UINT8(1, findWith(finder, world, &world.memory.tray[lastDie][0][0], GRAVITY_MM_PER_S2));
    world.dieCount = 1;
    TEST_ASSERT_EQUAL_UINT8(0, findWith(finder, world, nullptr, 0));
    world.dieCount = MAX_DICE;
    TEST_ASSERT_EQUAL_UINT8(1, findWith(finder, world, &world.memory.tray[lastDie][0][0], GRAVITY_MM_PER_S2));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_die_landing_flat_is_one_hit_of_its_whole_impulse);
    RUN_TEST(test_a_bouncing_die_hits_each_time_it_comes_down);
    RUN_TEST(test_a_die_resting_on_the_floor_makes_no_hits);
    RUN_TEST(test_two_dice_meeting_head_on_are_one_hit);
    RUN_TEST(test_dice_pressed_together_are_not_hit_again);
    RUN_TEST(test_a_contact_that_ends_on_a_vertex_out_of_play_hits_again);
    RUN_TEST(test_a_contact_that_ends_on_a_die_out_of_play_hits_again);
    return UNITY_END();
}

#include <math.h>
#include <unity.h>
#include "../../src/geometry/die_shape.h"
#include "../../src/geometry/die_shape.cpp"
#include "../../src/geometry/quaternion.h"
#include "../../src/geometry/quaternion.cpp"
#include "../../src/geometry/rotation.h"
#include "../../src/geometry/rotation.cpp"
#include "../../src/imu/shake_detector.h"
#include "../../src/imu/shake_detector.cpp"
#include "../../src/imu/reading_average.h"
#include "../../src/imu/reading_average.cpp"
#include "../../src/imu/still_span.h"
#include "../../src/imu/still_span.cpp"
#include "../../src/physics/die_result.h"
#include "../../src/physics/die_result.cpp"
#include "../../src/physics/launch.h"
#include "../../src/physics/launch.cpp"
#include "../../src/physics/reorientation.h"
#include "../../src/physics/reorientation.cpp"
#include "../../src/physics/tray_gravity.h"
#include "../../src/physics/tray_gravity.cpp"
#include "../../src/physics/world.h"
#include "../../src/physics/world.cpp"
#include "../../src/probes/fairness/seeded_source.h"
#include "../../src/roll/dice_roll.h"
#include "../../src/roll/dice_roll.cpp"
#include "../../src/roll/shake_roll.h"
#include "../../src/roll/shake_roll.cpp"

// Dice rolled by the device's own motion, as the device and the fairness test roll them (slice 0012).

static const uint64_t SEED = 1;
static const RestThresholds THRESHOLDS = PRODUCT_REST_THRESHOLDS;
static const uint32_t PERIOD_US = 10000;
static const uint32_t SAMPLES_PER_SECOND = 1000000 / PERIOD_US;
// Held tilted toward the face, 30° about x, as people hold the device; and held flat.
static const Vec3 TILTED{0, 0.5f, 0.866025f};
static const Vec3 FLAT{0, 0, 1.0f};
// Far past ADR-0005's half a g off 1 g, in turn either way along x, so every sample is busy.
static const Vec3 SWING_RIGHT{2.0f, 0, 1.0f};
static const Vec3 SWING_LEFT{-2.0f, 0, 1.0f};
// Half again the still span's spread off the held angle, either way in turn: never a still span,
// and about 0.01 g off 1 g, so never busy for the shake detector either. A hand that never settles.
static const float WAVER_OFFSET_G = 1.5f * STILL_SPREAD_G;
// Floats rotated through a quaternion carry a few ulps of error per component.
static const float GRAVITY_TOLERANCE_MM_PER_S2 = 1.0f;
// A step's own integration, against the same step done by hand: well under a degree apart.
static const float ALIGNMENT_TOLERANCE = 1e-4f;
static const Vec3 STRAIGHT_DOWN{0, 0, -GRAVITY_MM_PER_S2};
// Mid-tray, resting on the floor: far from every wall.
static const Vec3 MID_TRAY_MM{TRAY_WIDTH_MM / 2, TRAY_DEPTH_MM / 2, 0};
static const uint8_t RESTING_NUMERAL = 7;

static const DieShape D20 = makeD20(D20_EDGE_MM);

/** @brief One D20 at rest mid-tray, its last result locked, on a level tray. */
struct Scene {
    World         world{};
    DieResult     results[MAX_DICE];
    SeededSource  source{SEED};
    ShakeRoll     shakeRoll{PRODUCT_KICK};
    uint32_t      nextUs = 0;

    Scene() {
        world.shape = &D20;
        world.dieCount = 1;
        world.gravity = STRAIGHT_DOWN;
        world.dice[0] = {MID_TRAY_MM, {0, 0, 0}, Quaternion{1, 0, 0, 0}, {0, 0, 0}};
        results[0] = {DieState::Locked, RESTING_NUMERAL, 0};
    }

    /** @brief Feed @p count samples reading @p accelG; returns how many of them started a roll. */
    uint32_t feed(uint32_t count, Vec3 accelG) {
        uint32_t started = 0;
        for (uint32_t i = 0; i < count; i++) {
            const ImuSample sample{accelG, {0, 0, 0}, nextUs};
            if (shakeRoll.addSample(sample, world, results, source)) started++;
            nextUs += PERIOD_US;
        }
        return started;
    }

    /** @brief Feed @p count busy samples, swinging either way in turn. */
    uint32_t shake(uint32_t count) {
        uint32_t started = 0;
        for (uint32_t i = 0; i < count; i++) started += feed(1, i % 2 == 0 ? SWING_RIGHT : SWING_LEFT);
        return started;
    }

    /** @brief Feed @p count samples wavering about @p heldG, either way along x in turn. */
    void waver(uint32_t count, Vec3 heldG) {
        const Vec3 offset{WAVER_OFFSET_G, 0, 0};
        for (uint32_t i = 0; i < count; i++) feed(1, i % 2 == 0 ? heldG + offset : heldG - offset);
    }
};

/** @brief 1 when @p a and @p b are the same orientation, either sign, falling with the angle between them. */
static float alignment(Quaternion a, Quaternion b) { return fabsf(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z); }

static void assertGravity(Vec3 expected, Vec3 actual) {
    TEST_ASSERT_FLOAT_WITHIN(GRAVITY_TOLERANCE_MM_PER_S2, expected.x, actual.x);
    TEST_ASSERT_FLOAT_WITHIN(GRAVITY_TOLERANCE_MM_PER_S2, expected.y, actual.y);
    TEST_ASSERT_FLOAT_WITHIN(GRAVITY_TOLERANCE_MM_PER_S2, expected.z, actual.z);
}

void test_a_tilted_hold_reads_as_level() {
    Scene scene;
    scene.feed(SAMPLES_PER_SECOND, TILTED);
    TEST_ASSERT_TRUE(scene.shakeRoll.hasLevel());
    assertGravity(STRAIGHT_DOWN, scene.world.gravity);
}

void test_the_tray_stays_level_until_the_first_still_span() {
    // Without a level, the tilted reading would tilt the tray about an angle no hold ever set.
    Scene scene;
    scene.shake(2 * SAMPLES_PER_SECOND);
    TEST_ASSERT_FALSE(scene.shakeRoll.hasLevel());
    assertGravity(STRAIGHT_DOWN, scene.world.gravity);
    // The dice are in flight, where the level follows the average, but only once there is a level.
    TEST_ASSERT_EQUAL_FLOAT(1.0f, scene.shakeRoll.level().w);
}

void test_a_tilt_after_the_hold_tilts_the_tray() {
    // Held tilted, then laid flat: until the flat hold is a still span, the tray tilts by 30°.
    Scene scene;
    scene.feed(SAMPLES_PER_SECOND, TILTED);
    scene.feed(1, FLAT);
    const Vec3 tilted = trayGravity(levelFrom(TILTED), FLAT);
    assertGravity(tilted, scene.world.gravity);
    TEST_ASSERT_TRUE(fabsf(tilted.y) > GRAVITY_MM_PER_S2 / 4);
}

void test_a_shake_starts_one_roll_and_kicks_the_dice() {
    Scene scene;
    scene.feed(SAMPLES_PER_SECOND, TILTED);
    TEST_ASSERT_EQUAL_UINT32(1, scene.shake(2 * SAMPLES_PER_SECOND));
    TEST_ASSERT_EQUAL(DieState::Rolling, scene.results[0].state);
    // The throw: a die left alone by the shake would only feel the swinging gravity's pull.
    TEST_ASSERT_TRUE(length(scene.world.dice[0].velocity) > 0);
}

void test_a_shake_that_ends_and_starts_again_starts_a_second_roll() {
    Scene scene;
    scene.feed(SAMPLES_PER_SECOND, TILTED);
    TEST_ASSERT_EQUAL_UINT32(1, scene.shake(2 * SAMPLES_PER_SECOND));
    scene.feed(SAMPLES_PER_SECOND, TILTED);
    TEST_ASSERT_FALSE(scene.shakeRoll.shaking());
    TEST_ASSERT_EQUAL_UINT32(1, scene.shake(2 * SAMPLES_PER_SECOND));
}

void test_no_die_locks_while_the_shake_goes_on() {
    // The die is held still mid-shake: it would lock after the hold time if the shake let it.
    Scene scene;
    scene.feed(SAMPLES_PER_SECOND, TILTED);
    scene.shake(2 * SAMPLES_PER_SECOND);
    TEST_ASSERT_TRUE(scene.shakeRoll.shaking());
    scene.world.gravity = STRAIGHT_DOWN;
    scene.world.dice[0] = {MID_TRAY_MM, {0, 0, 0}, Quaternion{1, 0, 0, 0}, {0, 0, 0}};
    const uint32_t steps = 4 * stepsIn(D20.holdS);
    for (uint32_t step = 0; step < steps; step++) {
        TEST_ASSERT_EQUAL_UINT8(0, scene.shakeRoll.step(scene.world, scene.results, scene.source, THRESHOLDS));
        scene.world.dice[0].position = MID_TRAY_MM;
        scene.world.dice[0].velocity = {0, 0, 0};
        scene.world.dice[0].angularVelocity = {0, 0, 0};
    }
    TEST_ASSERT_EQUAL(DieState::Rolling, scene.results[0].state);
}

/** @brief A scene held at TILTED until it is level there, then launched: a roll in flight on a still device. */
static void launchFromTiltedHold(Scene& scene) {
    scene.feed(SAMPLES_PER_SECOND, TILTED);
    scene.shakeRoll.launch(scene.world, scene.results, 1, scene.source);
}

void test_a_roll_in_flight_follows_the_held_angle_by_its_average() {
    // ADR-0014: people recline the device to read once they stop shaking; the dice must not slide
    // toward the top while it settles.
    Scene scene;
    launchFromTiltedHold(scene);
    scene.feed(1, FLAT);
    const float share = 1 - expf(-(PERIOD_US / 1e6f) / READING_AVERAGE_TIME_CONSTANT_S);
    const Vec3 averaged = TILTED + (FLAT - TILTED) * share;
    assertGravity(trayGravity(levelFrom(averaged), FLAT), scene.world.gravity);
    // Ten time constants later the flat hold is level, though the die has not locked.
    scene.feed(SAMPLES_PER_SECOND, FLAT);
    TEST_ASSERT_NOT_EQUAL(DieState::Locked, scene.results[0].state);
    assertGravity(STRAIGHT_DOWN, scene.world.gravity);
}

void test_a_roll_in_flight_keeps_its_level_through_free_fall() {
    // Thrown, the device reads nothing: no direction to level by, so the level stays.
    Scene scene;
    launchFromTiltedHold(scene);
    scene.feed(SAMPLES_PER_SECOND, {0, 0, 0});
    // Then a reading at half the least a level is taken from: the average settles on it, pointing
    // FLAT, and only the threshold keeps the level from following. Half, not just under, so a
    // retuned time constant cannot carry the average over the threshold and fail this for nothing.
    scene.feed(SAMPLES_PER_SECOND, FLAT * (LEVEL_MIN_AVERAGE_G / 2));
    TEST_ASSERT_FLOAT_WITHIN(ALIGNMENT_TOLERANCE, 1.0f, alignment(scene.shakeRoll.level(), levelFrom(TILTED)));
}

void test_locked_dice_level_to_the_still_span_not_the_average() {
    // A quarter of a second still at a new angle, then one sample tilted back: the average has
    // moved toward that sample, the still span has not.
    Scene scene;
    scene.feed(SAMPLES_PER_SECOND, TILTED);
    scene.feed(SAMPLES_PER_SECOND, FLAT);
    scene.feed(1, TILTED);
    assertGravity(trayGravity(levelFrom(FLAT), TILTED), scene.world.gravity);
}

/**
 * @brief The roll in flight ends wavering at @p heldG and the die locks, the hand not still at that
 * angle; returns the level the roll ended on. The die is locked by hand: this is about the level,
 * not about how the die comes to rest.
 */
static Quaternion lockWhileWavering(Scene& scene, Vec3 heldG) {
    scene.waver(SAMPLES_PER_SECOND, heldG);
    const Quaternion rollLevel = scene.shakeRoll.level();
    scene.results[0] = {DieState::Locked, RESTING_NUMERAL, 0};
    scene.waver(1, heldG);
    return rollLevel;
}

void test_dice_locked_after_a_shake_keep_the_rolls_level_until_a_new_still_span() {
    // Slice 0018: the latest still span here is the hold before the shake; levelling to it at the
    // lock tilts the tray back 30 degrees under dice already at rest, and they slide.
    Scene scene;
    scene.feed(SAMPLES_PER_SECOND, TILTED);
    TEST_ASSERT_GREATER_THAN_UINT32(0, scene.shake(2 * SAMPLES_PER_SECOND));
    const Quaternion rollLevel = lockWhileWavering(scene, FLAT);
    TEST_ASSERT_FLOAT_WITHIN(ALIGNMENT_TOLERANCE, 1.0f, alignment(scene.shakeRoll.level(), rollLevel));
}

void test_dice_locked_after_a_launch_keep_the_rolls_level_until_a_new_still_span() {
    Scene scene;
    launchFromTiltedHold(scene);
    const Quaternion rollLevel = lockWhileWavering(scene, FLAT);
    TEST_ASSERT_FLOAT_WITHIN(ALIGNMENT_TOLERANCE, 1.0f, alignment(scene.shakeRoll.level(), rollLevel));
}

void test_dice_locked_after_a_hold_in_flight_keep_the_rolls_level_until_a_new_still_span() {
    // Held still at FLAT for a span while the die flies, then raised to TILTED to read: that span
    // is older than the angle the die locks at, and levelling to it tilts the tray as the hold
    // before the roll would.
    Scene scene;
    launchFromTiltedHold(scene);
    scene.feed(SAMPLES_PER_SECOND, FLAT);
    const Quaternion rollLevel = lockWhileWavering(scene, TILTED);
    TEST_ASSERT_FLOAT_WITHIN(ALIGNMENT_TOLERANCE, 1.0f, alignment(scene.shakeRoll.level(), rollLevel));
}

void test_a_still_span_after_the_lock_levels_the_locked_dice() {
    // Held still at the angle it was raised from: a new still span, so the level again.
    Scene scene;
    launchFromTiltedHold(scene);
    lockWhileWavering(scene, FLAT);
    scene.feed(SAMPLES_PER_SECOND, TILTED);
    assertGravity(STRAIGHT_DOWN, scene.world.gravity);
}

void test_a_launch_cancels_the_spin_in_progress() {
    // The spin sets the die's orientation before every step; left running, it would drag the
    // launched die back toward the turn it was making.
    Scene scene;
    scene.feed(SAMPLES_PER_SECOND, TILTED);
    scene.shake(2 * SAMPLES_PER_SECOND);
    scene.shakeRoll.launch(scene.world, scene.results, 1, scene.source);
    const DieBody launched = scene.world.dice[0];
    scene.shakeRoll.step(scene.world, scene.results, scene.source, THRESHOLDS);
    const Quaternion stepped = scene.world.dice[0].orientation;
    const Quaternion expected = integrate(launched.orientation, launched.angularVelocity, PHYSICS_STEP_S);
    TEST_ASSERT_FLOAT_WITHIN(ALIGNMENT_TOLERANCE, 1.0f, alignment(stepped, expected));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_tilted_hold_reads_as_level);
    RUN_TEST(test_the_tray_stays_level_until_the_first_still_span);
    RUN_TEST(test_a_tilt_after_the_hold_tilts_the_tray);
    RUN_TEST(test_a_shake_starts_one_roll_and_kicks_the_dice);
    RUN_TEST(test_a_shake_that_ends_and_starts_again_starts_a_second_roll);
    RUN_TEST(test_no_die_locks_while_the_shake_goes_on);
    RUN_TEST(test_a_roll_in_flight_follows_the_held_angle_by_its_average);
    RUN_TEST(test_a_roll_in_flight_keeps_its_level_through_free_fall);
    RUN_TEST(test_locked_dice_level_to_the_still_span_not_the_average);
    RUN_TEST(test_dice_locked_after_a_shake_keep_the_rolls_level_until_a_new_still_span);
    RUN_TEST(test_dice_locked_after_a_launch_keep_the_rolls_level_until_a_new_still_span);
    RUN_TEST(test_dice_locked_after_a_hold_in_flight_keep_the_rolls_level_until_a_new_still_span);
    RUN_TEST(test_a_still_span_after_the_lock_levels_the_locked_dice);
    RUN_TEST(test_a_launch_cancels_the_spin_in_progress);
    return UNITY_END();
}

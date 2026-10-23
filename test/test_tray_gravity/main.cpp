#include <math.h>
#include <unity.h>
#include "../../src/geometry/quaternion.h"
#include "../../src/geometry/quaternion.cpp"
#include "../../src/geometry/rotation.h"
#include "../../src/geometry/rotation.cpp"
#include "../../src/physics/tray_gravity.h"
#include "../../src/physics/tray_gravity.cpp"

// One µg of a 9810 mm/s² gravity: float rounding through a rotation, not a physical tolerance.
static const float TOLERANCE_MM_PER_S2 = 0.01f;
// How people hold a handheld: tilted toward their face, its top edge raised.
static const float HOLDING_DEG = 35.0f;
static const float FURTHER_TILT_DEG = 10.0f;

static const Vec3 FLAT{0, 0, 1};

static void assertVec(Vec3 expected, Vec3 actual) {
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE_MM_PER_S2, expected.x, actual.x);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE_MM_PER_S2, expected.y, actual.y);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE_MM_PER_S2, expected.z, actual.z);
}

/** @brief A still reading with the device's top edge raised by @p degrees. */
static Vec3 raisedTop(float degrees) {
    const float angle = degrees * RAD_PER_DEG;
    return {0, sinf(angle), cosf(angle)};
}

void test_a_flat_still_device_pulls_the_dice_straight_down() {
    assertVec({0, 0, -GRAVITY_MM_PER_S2}, trayGravity(levelFrom(FLAT), FLAT));
}

void test_the_holding_angle_is_level() {
    const Vec3 holding = raisedTop(HOLDING_DEG);
    assertVec({0, 0, -GRAVITY_MM_PER_S2}, trayGravity(levelFrom(holding), holding));
}

void test_raising_the_top_further_slides_the_dice_toward_the_bottom_edge() {
    const Quaternion level = levelFrom(raisedTop(HOLDING_DEG));
    const float tilt = FURTHER_TILT_DEG * RAD_PER_DEG;
    assertVec({0, -GRAVITY_MM_PER_S2 * sinf(tilt), -GRAVITY_MM_PER_S2 * cosf(tilt)},
              trayGravity(level, raisedTop(HOLDING_DEG + FURTHER_TILT_DEG)));
}

void test_a_push_to_the_right_throws_the_dice_to_the_left() {
    // The device accelerating at 2 g toward +x reads 2 g on x; the dice, left behind, meet the
    // left wall as if gravity pulled them there.
    assertVec({-2 * GRAVITY_MM_PER_S2, 0, -GRAVITY_MM_PER_S2}, trayGravity(levelFrom(FLAT), {2, 0, 1}));
}

void test_a_device_held_face_down_is_level_too() {
    const Vec3 faceDown{0, 0, -1};
    assertVec({0, 0, -GRAVITY_MM_PER_S2}, trayGravity(levelFrom(faceDown), faceDown));
}

void test_a_reading_off_one_g_keeps_its_size() {
    // Levelling turns the reading; it must not rescale a shake.
    const Vec3 reading{0.3f, -1.2f, 2.5f};
    const Vec3 gravity = trayGravity(levelFrom(raisedTop(HOLDING_DEG)), reading);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE_MM_PER_S2, length(reading) * GRAVITY_MM_PER_S2, length(gravity));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_flat_still_device_pulls_the_dice_straight_down);
    RUN_TEST(test_the_holding_angle_is_level);
    RUN_TEST(test_raising_the_top_further_slides_the_dice_toward_the_bottom_edge);
    RUN_TEST(test_a_push_to_the_right_throws_the_dice_to_the_left);
    RUN_TEST(test_a_device_held_face_down_is_level_too);
    RUN_TEST(test_a_reading_off_one_g_keeps_its_size);
    return UNITY_END();
}

#include <unity.h>
#include "../../src/imu/axis_map.h"
#include "../../src/imu/axis_map.cpp"

static const float TOLERANCE = 1e-6f;
// Distinct per axis, so selecting the wrong sensor axis cannot give the right number.
static const Vec3 SENSOR{1.0f, 2.0f, 3.0f};

static constexpr AxisSource plus(SensorAxis axis) { return {axis, AxisSign::Positive}; }
static constexpr AxisSource minus(SensorAxis axis) { return {axis, AxisSign::Negative}; }

static constexpr AxisMap IDENTITY{plus(SensorAxis::X), plus(SensorAxis::Y), plus(SensorAxis::Z)};

static void assertVec(float x, float y, float z, const Vec3& actual) {
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, x, actual.x);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, y, actual.y);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, z, actual.z);
}

void test_identity_map_leaves_the_vector_alone() {
    assertVec(1.0f, 2.0f, 3.0f, remapToDeviceFrame(SENSOR, IDENTITY));
}

void test_each_device_axis_takes_its_own_sensor_axis() {
    // A cyclic shift: device x <- sensor y, y <- z, z <- x.
    const AxisMap shifted{plus(SensorAxis::Y), plus(SensorAxis::Z), plus(SensorAxis::X)};
    assertVec(2.0f, 3.0f, 1.0f, remapToDeviceFrame(SENSOR, shifted));
}

void test_each_axis_sign_is_applied_on_its_own() {
    assertVec(-1.0f, -2.0f, 3.0f,
              remapToDeviceFrame(SENSOR, {minus(SensorAxis::X), minus(SensorAxis::Y), plus(SensorAxis::Z)}));
    assertVec(-1.0f, 2.0f, -3.0f,
              remapToDeviceFrame(SENSOR, {minus(SensorAxis::X), plus(SensorAxis::Y), minus(SensorAxis::Z)}));
    assertVec(1.0f, -2.0f, -3.0f,
              remapToDeviceFrame(SENSOR, {plus(SensorAxis::X), minus(SensorAxis::Y), minus(SensorAxis::Z)}));
}

void test_a_swap_with_one_sign_flip_is_a_rotation() {
    // A quarter turn about z: device x <- sensor y, device y <- -sensor x.
    const AxisMap quarterTurn{plus(SensorAxis::Y), minus(SensorAxis::X), plus(SensorAxis::Z)};
    TEST_ASSERT_TRUE(isRotation(quarterTurn));
    assertVec(2.0f, -1.0f, 3.0f, remapToDeviceFrame(SENSOR, quarterTurn));
}

void test_rotations_are_accepted() {
    TEST_ASSERT_TRUE(isRotation(IDENTITY));
    TEST_ASSERT_TRUE(isRotation({plus(SensorAxis::Y), plus(SensorAxis::Z), plus(SensorAxis::X)}));
    // A half turn about z flips two signs, which keeps the handedness.
    TEST_ASSERT_TRUE(isRotation({minus(SensorAxis::X), minus(SensorAxis::Y), plus(SensorAxis::Z)}));
}

void test_a_map_that_mirrors_is_rejected() {
    // One sign dropped from a valid map: the slip this check exists to catch.
    TEST_ASSERT_FALSE(isRotation({minus(SensorAxis::X), plus(SensorAxis::Y), plus(SensorAxis::Z)}));
    // Two axes swapped with no sign flip.
    TEST_ASSERT_FALSE(isRotation({plus(SensorAxis::Y), plus(SensorAxis::X), plus(SensorAxis::Z)}));
}

void test_a_map_that_reuses_a_sensor_axis_is_rejected() {
    TEST_ASSERT_FALSE(isRotation({plus(SensorAxis::X), plus(SensorAxis::X), plus(SensorAxis::Z)}));
    TEST_ASSERT_FALSE(isRotation({plus(SensorAxis::Z), minus(SensorAxis::Y), plus(SensorAxis::Z)}));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_identity_map_leaves_the_vector_alone);
    RUN_TEST(test_each_device_axis_takes_its_own_sensor_axis);
    RUN_TEST(test_each_axis_sign_is_applied_on_its_own);
    RUN_TEST(test_a_swap_with_one_sign_flip_is_a_rotation);
    RUN_TEST(test_rotations_are_accepted);
    RUN_TEST(test_a_map_that_mirrors_is_rejected);
    RUN_TEST(test_a_map_that_reuses_a_sensor_axis_is_rejected);
    return UNITY_END();
}

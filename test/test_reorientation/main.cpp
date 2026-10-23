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
#include "../../src/physics/reorientation.h"
#include "../../src/physics/reorientation.cpp"
#include "../../src/physics/world.h"
#include "../../src/physics/world.cpp"
#include "../../src/probes/fairness/seeded_source.h"

static const uint64_t SEED = 1;
static const float TURN_S = 0.1f;
static const uint32_t TURN_STEPS = stepsIn(TURN_S);
static const float TOLERANCE = 1e-4f;
static const float PI_RAD = 3.14159265f;
// The face toward the glass after the turn, over this many turns, is tested the way invariant #2
// tests results: chi-square at α = 0.001 against uniform, 19 degrees of freedom for a D20.
static const uint32_t TURNS = 20000;
static const double CHI_SQUARE_CRITICAL_D20 = 43.820;

static const DieShape D20 = makeD20(D20_EDGE_MM);

static DieBody restingWith(Quaternion orientation) {
    DieBody body{};
    body.orientation = orientation;
    return body;
}

/** @brief Run a whole turn on @p body with nothing else moving it: a call per step, and one to end it. */
static void turnFully(Reorientation& turn, DieBody& body) {
    for (uint32_t step = 0; step <= TURN_STEPS; step++) continueReorientation(turn, body);
}

static double upFaceChiSquare(Quaternion start) {
    SeededSource source(SEED);
    uint32_t counts[DIE_MAX_FACES] = {};
    for (uint32_t i = 0; i < TURNS; i++) {
        DieBody body = restingWith(start);
        Reorientation turn = startReorientation(body, TURN_S, source);
        turnFully(turn, body);
        counts[upFace(D20, body.orientation).face]++;
    }
    const double expected = static_cast<double>(TURNS) / D20.faceCount;
    double statistic = 0;
    for (uint8_t face = 0; face < D20.faceCount; face++) {
        const double deviation = counts[face] - expected;
        statistic += deviation * deviation / expected;
    }
    return statistic;
}

void test_the_face_up_after_a_turn_is_uniform_whatever_the_start() {
    // The previous result must not pull the next: from a die lying on any face, every face is
    // equally likely to come up once the turn ends.
    TEST_ASSERT_LESS_THAN_DOUBLE(CHI_SQUARE_CRITICAL_D20, upFaceChiSquare(IDENTITY_QUATERNION));
    TEST_ASSERT_LESS_THAN_DOUBLE(CHI_SQUARE_CRITICAL_D20, upFaceChiSquare(quaternionAbout({1, 2, 3}, 2.0f)));
}

void test_the_turn_spins_at_one_rate_about_one_axis_and_no_more_than_half_a_turn() {
    SeededSource source(SEED);
    DieBody body = restingWith(quaternionAbout({0, 1, 0}, 1.0f));
    Reorientation turn = startReorientation(body, TURN_S, source);
    TEST_ASSERT_TRUE(turn.angleRad <= PI_RAD);
    continueReorientation(turn, body);
    const Vec3 spin = body.angularVelocity;
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, turn.angleRad / (TURN_STEPS * PHYSICS_STEP_S), length(spin));
    for (uint32_t step = 1; step < TURN_STEPS; step++) {
        continueReorientation(turn, body);
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, 0, length(body.angularVelocity - spin));
    }
}

void test_the_die_is_left_with_its_own_spin_once_the_time_is_up() {
    SeededSource source(SEED);
    DieBody body = restingWith(IDENTITY_QUATERNION);
    Reorientation turn = startReorientation(body, TURN_S, source);
    turnFully(turn, body);
    TEST_ASSERT_FALSE(turn.active);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, 0, length(body.angularVelocity - turn.finalAngularVelocity));
    const float spin = length(turn.finalAngularVelocity);
    TEST_ASSERT_TRUE(spin >= SPIN_MIN_RAD_PER_S && spin <= SPIN_MAX_RAD_PER_S);

    // Over: the physics moves the die from here, and the turn no longer touches it.
    const DieBody after = body;
    body.angularVelocity = {0, 0, 0};
    continueReorientation(turn, body);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, 0, length(body.angularVelocity));
    TEST_ASSERT_EQUAL_FLOAT(after.orientation.w, body.orientation.w);
}

void test_the_turn_overrides_what_a_collision_does_to_the_orientation_meanwhile() {
    // Contacts may twist the die during a step; the turn must still end where it was drawn to, or
    // the end orientation would no longer be uniform.
    SeededSource first(SEED);
    SeededSource second(SEED);
    DieBody undisturbed = restingWith(IDENTITY_QUATERNION);
    DieBody knocked = restingWith(IDENTITY_QUATERNION);
    Reorientation calm = startReorientation(undisturbed, TURN_S, first);
    Reorientation bumped = startReorientation(knocked, TURN_S, second);
    for (uint32_t step = 0; step <= TURN_STEPS; step++) {
        knocked.orientation = knocked.orientation * quaternionAbout({1, 0, 0}, 0.3f);
        continueReorientation(calm, undisturbed);
        continueReorientation(bumped, knocked);
    }
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, undisturbed.orientation.w, knocked.orientation.w);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, undisturbed.orientation.x, knocked.orientation.x);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, undisturbed.orientation.y, knocked.orientation.y);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, undisturbed.orientation.z, knocked.orientation.z);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_face_up_after_a_turn_is_uniform_whatever_the_start);
    RUN_TEST(test_the_turn_spins_at_one_rate_about_one_axis_and_no_more_than_half_a_turn);
    RUN_TEST(test_the_die_is_left_with_its_own_spin_once_the_time_is_up);
    RUN_TEST(test_the_turn_overrides_what_a_collision_does_to_the_orientation_meanwhile);
    return UNITY_END();
}

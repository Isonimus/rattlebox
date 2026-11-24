#include <initializer_list>
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
#include "../../src/physics/world.h"
#include "../../src/physics/world.cpp"

// Rest detection and the lock (ADR-0002), and invariant #3: a locked result never changes until
// the next roll.

static const float TOLERANCE = 1e-4f;
static const RestThresholds THRESHOLDS = PRODUCT_REST_THRESHOLDS;
static const uint32_t SECOND = stepsIn(1.0f);
static const uint8_t TOP_FACE = 7;
// A lean well past the cocked tolerance, as a die propped on a wall measured at 1° to 28°.
static const float PROPPED_TILT_RAD = 10 * RAD_PER_DEG;
static const float STRIKE_MM_PER_S = 2000.0f;

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

/** @brief The orientation that turns @p face of @p shape straight toward the glass. */
static Quaternion faceUp(const DieShape& shape, uint8_t face) {
    const Vec3 normal = shape.faces[face].normal;
    return quaternionAbout(cross(normal, {0, 0, 1}), acosf(normal.z));
}

/** @brief The orientation that turns @p face of @p shape straight at the tray floor. */
static Quaternion faceDown(const DieShape& shape, uint8_t face) {
    const Vec3 normal = shape.faces[face].normal;
    return quaternionAbout(cross(normal, {0, 0, -1}), acosf(-normal.z));
}

static DieBody still(Quaternion orientation) { return {{50, 30, 10}, {0, 0, 0}, orientation, {0, 0, 0}}; }

static DieResult feed(DieResult result, const DieBody& body, uint32_t steps) {
    for (uint32_t step = 0; step < steps; step++) result = updateResult(result, body, D20, THRESHOLDS);
    return result;
}

static DieResult feedShape(const DieShape& shape, const DieBody& body, uint32_t steps) {
    DieResult result = startRoll();
    for (uint32_t step = 0; step < steps; step++) result = updateResult(result, body, shape, THRESHOLDS);
    return result;
}

void test_each_die_waits_out_its_own_hold_and_not_another_dies() {
    // The hold is a property of the body, not of the product (ADR-0028): a D6 drops onto the next
    // face at once and locks after 0.01 s, while a D10 can balance on an edge and waits 0.07 s.
    const DieShape d6 = makeD6(D6_EDGE_MM);
    const DieShape d10 = makeD10(D10_CIRCUMRADIUS_MM);
    TEST_ASSERT_TRUE_MESSAGE(d6.holdS < d10.holdS, "a D6 should not wait as long as a D10");

    const uint32_t d6Steps = stepsToHold(d6.holdS);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DieState::Locked),
                            static_cast<uint8_t>(feedShape(d6, still(faceUp(d6, 0)), d6Steps).state));
    // The same stillness is nowhere near enough for the D10, where one shared hold would have
    // locked both or made the D6 wait for the D10.
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DieState::Rolling),
                            static_cast<uint8_t>(feedShape(d10, still(faceUp(d10, 0)), d6Steps).state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DieState::Locked),
                            static_cast<uint8_t>(feedShape(d10, still(faceUp(d10, 0)), stepsToHold(d10.holdS)).state));
}

void test_a_d4_locks_to_the_face_it_is_resting_on_not_to_one_turned_to_the_glass() {
    // ADR-0033: a tetrahedron presents no face to the glass, so its result is the hidden one it
    // came to rest on, which is read at the apex standing over it.
    const DieShape d4 = makeD4(D4_EDGE_MM);
    for (uint8_t face = 0; face < d4.faceCount; face++) {
        const DieResult result = feedShape(d4, still(faceDown(d4, face)), stepsToHold(d4.holdS));
        TEST_ASSERT_EQUAL(DieState::Locked, result.state);
        TEST_ASSERT_EQUAL_UINT8(d4.faces[face].numeral, result.numeral);
    }
}

void test_a_d4_lying_perfectly_flat_is_nowhere_near_flat_by_the_top_face_rule() {
    // Why the D4 could not simply reuse the rule the other five dice share: at rest its nearest
    // face to the glass is 70.5° away, against a 0.5° tolerance, so the old rule would have
    // called every lying-flat D4 cocked and nudged it forever (ADR-0033).
    const DieShape d4 = makeD4(D4_EDGE_MM);
    const Quaternion resting = faceDown(d4, 0);
    TEST_ASSERT_TRUE(upFace(d4, resting).tiltRad > THRESHOLDS.cockedTiltRad);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, acosf(1.0f / 3), upFace(d4, resting).tiltRad);
    // The rule it does use reads the same pose as perfectly flat.
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0, readFace(d4, resting).tiltRad);
}

void test_a_propped_d4_is_cocked_and_never_locks() {
    const DieShape d4 = makeD4(D4_EDGE_MM);
    const Quaternion propped = quaternionAbout({1, 0, 0}, PROPPED_TILT_RAD) * faceDown(d4, 0);
    const DieResult result = feedShape(d4, still(propped), stepsToHold(d4.holdS) * 2);
    TEST_ASSERT_EQUAL(DieState::Cocked, result.state);
}

void test_every_top_face_die_still_reads_the_face_turned_to_the_glass() {
    // readFace dispatches on the shape's declaration, so all five symmetric dice must be
    // untouched by the D4's arrival: for each of them it must agree with upFace, face by face.
    for (const DieShape& shape : {makeD6(D6_EDGE_MM), makeD8(D8_EDGE_MM), makeD10(D10_CIRCUMRADIUS_MM),
                                  makeD12(D12_EDGE_MM), makeD20(D20_EDGE_MM)}) {
        TEST_ASSERT_EQUAL(NumeralReading::TopFace, shape.reading);
        for (uint8_t face = 0; face < shape.faceCount; face++) {
            // Which face comes back is not asserted, because faceUp cannot pose one whose normal
            // is already straight down: its axis is cross(normal, +z), which is zero there. The
            // claim here is that the two readings agree, and they are fed the same pose whether
            // or not that pose is the one asked for.
            const Quaternion pose = faceUp(shape, face);
            TEST_ASSERT_EQUAL_UINT8(upFace(shape, pose).face, readFace(shape, pose).face);
        }
    }
}

void test_the_up_face_is_the_one_turned_to_the_glass() {
    const UpFace top = upFace(D20, faceUp(D20, TOP_FACE));
    TEST_ASSERT_EQUAL_UINT8(TOP_FACE, top.face);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0, top.tiltRad);
}

void test_a_die_still_for_the_hold_time_locks_to_its_up_face() {
    const DieResult result = feed(startRoll(), still(faceUp(D20, TOP_FACE)), HOLD_STEPS);
    TEST_ASSERT_EQUAL(DieState::Locked, result.state);
    TEST_ASSERT_EQUAL_UINT8(D20.faces[TOP_FACE].numeral, result.numeral);
}

void test_a_die_still_for_less_than_the_hold_time_is_still_rolling() {
    const DieResult result = feed(startRoll(), still(faceUp(D20, TOP_FACE)), HOLD_STEPS - 1);
    TEST_ASSERT_EQUAL(DieState::Rolling, result.state);
}

void test_moving_past_either_threshold_starts_the_hold_over() {
    DieBody sliding = still(faceUp(D20, TOP_FACE));
    sliding.velocity = {THRESHOLDS.linearMmPerS * 2, 0, 0};
    DieBody turning = still(faceUp(D20, TOP_FACE));
    turning.angularVelocity = {0, 0, THRESHOLDS.angularRadPerS * 2};
    for (const DieBody& moving : {sliding, turning}) {
        DieResult result = feed(startRoll(), still(faceUp(D20, TOP_FACE)), HOLD_STEPS - 1);
        result = feed(result, moving, 1);
        result = feed(result, still(faceUp(D20, TOP_FACE)), HOLD_STEPS - 1);
        TEST_ASSERT_EQUAL(DieState::Rolling, result.state);
    }
}

void test_a_die_resting_propped_is_cocked_not_locked() {
    const Quaternion leaning = quaternionAbout({1, 0, 0}, PROPPED_TILT_RAD) * faceUp(D20, TOP_FACE);
    const DieResult result = feed(startRoll(), still(leaning), HOLD_STEPS);
    TEST_ASSERT_EQUAL(DieState::Cocked, result.state);
}

void test_a_lean_inside_the_cocked_tolerance_still_locks() {
    const Quaternion nearlyFlat = quaternionAbout({1, 0, 0}, THRESHOLDS.cockedTiltRad / 2) * faceUp(D20, TOP_FACE);
    const DieResult result = feed(startRoll(), still(nearlyFlat), HOLD_STEPS);
    TEST_ASSERT_EQUAL(DieState::Locked, result.state);
    TEST_ASSERT_EQUAL_UINT8(D20.faces[TOP_FACE].numeral, result.numeral);
}

void test_a_locked_result_survives_any_later_motion_or_pose() {
    // Invariant #3: whatever the die does after it locks, fed body by body.
    const DieResult locked = feed(startRoll(), still(faceUp(D20, TOP_FACE)), HOLD_STEPS);
    TEST_ASSERT_EQUAL(DieState::Locked, locked.state);
    for (uint8_t face = 0; face < D20.faceCount; face++) {
        DieBody tumbling = still(faceUp(D20, face));
        tumbling.velocity = {STRIKE_MM_PER_S, 0, 0};
        tumbling.angularVelocity = {0, 30, 0};
        const DieResult after = feed(feed(locked, tumbling, SECOND), still(faceUp(D20, face)), SECOND);
        TEST_ASSERT_EQUAL(DieState::Locked, after.state);
        TEST_ASSERT_EQUAL_UINT8(locked.numeral, after.numeral);
    }
}

void test_a_locked_die_struck_and_tipped_by_the_physics_keeps_its_result() {
    // Invariant #3 through the real physics: the die locks on the floor, a strike tumbles it onto
    // other faces, and the result stays the first one.
    World world{};
    world.shape = &D20;
    world.gravity = {0, 0, -GRAVITY_MM_PER_S2};
    world.dieCount = 1;
    world.dice[0] = {{PRODUCT_TRAY.widthMm / 2, PRODUCT_TRAY.depthMm / 2, 20}, {0, 0, 0}, faceUp(D20, TOP_FACE), {0, 0, 0}};
    DieResult result = startRoll();
    for (uint32_t step = 0; step < SECOND && result.state != DieState::Locked; step++) {
        stepWorld(world, PRODUCT_TRAY);
        result = updateResult(result, world.dice[0], D20, THRESHOLDS);
    }
    TEST_ASSERT_EQUAL(DieState::Locked, result.state);
    const uint8_t lockedNumeral = result.numeral;

    world.dice[0].velocity = {STRIKE_MM_PER_S, STRIKE_MM_PER_S / 2, STRIKE_MM_PER_S / 2};
    world.dice[0].angularVelocity = {20, -15, 5};
    bool faceChanged = false;
    for (uint32_t step = 0; step < 3 * SECOND; step++) {
        stepWorld(world, PRODUCT_TRAY);
        result = updateResult(result, world.dice[0], D20, THRESHOLDS);
        if (D20.faces[upFace(D20, world.dice[0].orientation).face].numeral != lockedNumeral) faceChanged = true;
        TEST_ASSERT_EQUAL(DieState::Locked, result.state);
        TEST_ASSERT_EQUAL_UINT8(lockedNumeral, result.numeral);
    }
    // The strike must actually have turned the die over, or this proves nothing.
    TEST_ASSERT_TRUE(faceChanged);
}

void test_only_a_new_roll_clears_a_lock() {
    const DieResult locked = feed(startRoll(), still(faceUp(D20, TOP_FACE)), HOLD_STEPS);
    TEST_ASSERT_EQUAL(DieState::Locked, locked.state);
    const DieResult fresh = startRoll();
    TEST_ASSERT_EQUAL(DieState::Rolling, fresh.state);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, 0, fresh.stillS);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_each_die_waits_out_its_own_hold_and_not_another_dies);
    RUN_TEST(test_a_d4_locks_to_the_face_it_is_resting_on_not_to_one_turned_to_the_glass);
    RUN_TEST(test_a_d4_lying_perfectly_flat_is_nowhere_near_flat_by_the_top_face_rule);
    RUN_TEST(test_a_propped_d4_is_cocked_and_never_locks);
    RUN_TEST(test_every_top_face_die_still_reads_the_face_turned_to_the_glass);
    RUN_TEST(test_the_up_face_is_the_one_turned_to_the_glass);
    RUN_TEST(test_a_die_still_for_the_hold_time_locks_to_its_up_face);
    RUN_TEST(test_a_die_still_for_less_than_the_hold_time_is_still_rolling);
    RUN_TEST(test_moving_past_either_threshold_starts_the_hold_over);
    RUN_TEST(test_a_die_resting_propped_is_cocked_not_locked);
    RUN_TEST(test_a_lean_inside_the_cocked_tolerance_still_locks);
    RUN_TEST(test_a_locked_result_survives_any_later_motion_or_pose);
    RUN_TEST(test_a_locked_die_struck_and_tipped_by_the_physics_keeps_its_result);
    RUN_TEST(test_only_a_new_roll_clears_a_lock);
    return UNITY_END();
}

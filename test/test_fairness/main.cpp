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
#include "../../src/probes/fairness/simulated_roll.h"
#include "../../src/probes/fairness/simulated_roll.cpp"

// Invariant #2 (ADR-0002): a die type ships only with a passing chi-square goodness-of-fit test
// over simulated rolls through the real physics. Sized in slice 0008 to catch one face 1.25× too
// likely with 99% power at α = 0.001: 18,000 D20 results and 3,400 D6 results. The seed is fixed,
// so the test cannot flake; the settings and figures are ADR-0009's.

static const uint64_t SEED = 1;
static const unsigned long D20_RESULTS = 18000;
static const unsigned long D6_RESULTS = 3400;
// Upper 0.1% points of chi-square: 19 degrees of freedom for a D20, 5 for a D6.
static const double CRITICAL_D20 = 43.820;
static const double CRITICAL_D6 = 20.515;
// A roll still moving after this long has a die the physics never brings to rest.
static const float TIME_LIMIT_S = 20.0f;
// The planted bias: a D20 whose body sits 0.25 mm (2.4% of its edge) off its centre of mass, a
// loaded die. Measured in slice 0008, it scored 216 against the critical 43.8.
static const float LOAD_MM = 0.25f;

struct Tally {
    unsigned long counts[DIE_MAX_FACES + 1];
    unsigned long results;
    unsigned long unsettled;
};

static Tally roll(const DieShape& shape, uint8_t dice, unsigned long results) {
    SeededSource source(SEED);
    Tally tally{};
    while (tally.results < results) {
        const RollOutcome outcome = simulateRoll(shape, dice, source, PRODUCT_REST_THRESHOLDS, TIME_LIMIT_S, nullptr);
        // Stop at the first roll that never settles: dice that stop locking would otherwise keep
        // this loop going forever instead of failing.
        if (!outcome.settled) {
            tally.unsettled++;
            return tally;
        }
        for (uint8_t die = 0; die < dice; die++) {
            if (outcome.results[die].state != DieState::Locked) continue;
            tally.counts[outcome.results[die].numeral]++;
            tally.results++;
        }
    }
    return tally;
}

static double chiSquare(const Tally& tally, uint8_t faces) {
    const double expected = static_cast<double>(tally.results) / faces;
    double statistic = 0;
    for (uint8_t numeral = 1; numeral <= faces; numeral++) {
        const double deviation = tally.counts[numeral] - expected;
        statistic += deviation * deviation / expected;
    }
    return statistic;
}

static void assertFair(const DieShape& shape, uint8_t dice, unsigned long results, double critical) {
    const Tally tally = roll(shape, dice, results);
    // Every roll ends with every die locked, nudged off any wall it came to rest against.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, tally.unsettled, "a roll never came to rest");
    TEST_ASSERT_LESS_THAN_DOUBLE_MESSAGE(critical, chiSquare(tally, shape.faceCount), "results are not uniform");
}

void test_the_chi_square_statistic_matches_a_worked_example() {
    // 60 rolls of a D6 as 5, 8, 9, 8, 10, 20: expected 10 each, so the statistic is
    // (25 + 4 + 1 + 4 + 0 + 100) / 10 = 13.4.
    Tally tally{{0, 5, 8, 9, 8, 10, 20}, 60, 0};
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 13.4, chiSquare(tally, 6));
}

void test_one_d20_at_a_time_is_fair() { assertFair(makeD20(D20_EDGE_MM), 1, D20_RESULTS, CRITICAL_D20); }
void test_three_d20s_at_a_time_are_fair() { assertFair(makeD20(D20_EDGE_MM), MAX_DICE, D20_RESULTS, CRITICAL_D20); }
void test_one_d6_at_a_time_is_fair() { assertFair(makeD6(D6_EDGE_MM), 1, D6_RESULTS, CRITICAL_D6); }
void test_three_d6s_at_a_time_are_fair() { assertFair(makeD6(D6_EDGE_MM), MAX_DICE, D6_RESULTS, CRITICAL_D6); }

void test_a_loaded_d20_fails() {
    // Shows the test can fail: shifting the body off the centre of mass weights one side, and that
    // die lands heavy side down more often.
    DieShape loaded = makeD20(D20_EDGE_MM);
    for (uint8_t vertex = 0; vertex < loaded.vertexCount; vertex++) {
        loaded.vertices[vertex] = loaded.vertices[vertex] + loaded.faces[0].normal * LOAD_MM;
    }
    const Tally tally = roll(loaded, 1, D20_RESULTS);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, tally.unsettled, "a roll never came to rest");
    TEST_ASSERT_GREATER_THAN_DOUBLE(CRITICAL_D20, chiSquare(tally, loaded.faceCount));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_chi_square_statistic_matches_a_worked_example);
    RUN_TEST(test_one_d20_at_a_time_is_fair);
    RUN_TEST(test_three_d20s_at_a_time_are_fair);
    RUN_TEST(test_one_d6_at_a_time_is_fair);
    RUN_TEST(test_three_d6s_at_a_time_are_fair);
    RUN_TEST(test_a_loaded_d20_fails);
    return UNITY_END();
}

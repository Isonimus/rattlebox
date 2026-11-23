#include <math.h>
#include <unity.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
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
#include "../../src/probes/fairness/shaken_roll.h"
#include "../../src/probes/fairness/shaken_roll.cpp"
#include "../../src/probes/fairness/simulated_roll.h"
#include "../../src/probes/fairness/simulated_roll.cpp"
#include "../../src/roll/dice_roll.h"
#include "../../src/roll/dice_roll.cpp"
#include "../../src/roll/shake_roll.h"
#include "../../src/roll/shake_roll.cpp"

// Invariant #2 (ADR-0002, ADR-0009, ADR-0010): a die type ships only with a passing fairness test
// over rolls through the real physics, rolled as the device rolls them: one after another, each
// starting where the last one left the dice, moved by the IMU probe's recorded shakes through the
// device's own ShakeRoll, each shake after 1 s held still (slice 0012). Two checks at α = 0.001:
// chi-square goodness of fit of the results, and the share of results on the face the die showed
// before, against 1 in faces. Chi-square cannot see the second: a throw alone passed it while a D6
// came back on its previous face 24-28% of the time.
//
// Sized in slice 0008 to catch one face 1.25× too likely with 99% power: 18,000 D20 results and
// 3,400 D6 results, and in slice 0026 by the same calculation 5,100 D8 results. The seed is fixed, so the test cannot flake. One die at a time runs in
// `npm test`; three at a time take minutes more and run with `npm run test:fairness`, which
// defines FAIRNESS_THREE_DICE, before a change to the physics or its input merges (ADR-0010).

static const uint64_t SEED = 1;
static const unsigned long D20_RESULTS = 18000;
static const unsigned long D6_RESULTS = 3400;
static const unsigned long D8_RESULTS = 5100;
// Upper 0.1% points of chi-square: 19 degrees of freedom for a D20, 5 for a D6, 7 for a D8.
static const double CRITICAL_D20 = 43.820;
static const double CRITICAL_D6 = 20.515;
static const double CRITICAL_D8 = 24.322;
// The two-sided 0.1% point of the normal distribution, for the repeat share's binomial count.
static const double CRITICAL_REPEAT_Z = 3.2905;
// A roll still moving after this long has a die the physics never brings to rest.
static const float TIME_LIMIT_S = 20.0f;
// The planted bias: a D20 whose body sits 0.25 mm (2.4% of its edge) off its centre of mass, a
// loaded die. Under chained shakes it scored 590 against the critical 43.8; a D6 loaded as far
// scored 11.0 and passed, and 0.5 mm only 28.8 against 20.5, too close to keep failing after a retune.
static const float LOAD_MM = 0.25f;

// The IMU probe's shake takes at the product's 100 Hz (ADR-0005), from both boards: the operator's
// gentle and hard ones, and other people's (slice 0023). Every one in the directory is rolled on, so
// a new take needs no second list to keep in step. PlatformIO runs the test from the project
// directory.
static const char* const SHAKES_DIR = "test/fixtures/shakes";

/**
 * @brief Every .csv in @p dir, in byte order of their names. A chained roll picks its take by index
 * from the seeded source, so the order is part of the result and may not follow the file system's.
 */
static std::vector<std::string> shakeFiles(const std::filesystem::path& dir) {
    std::vector<std::string> paths;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".csv") paths.push_back(entry.path().generic_string());
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

static std::vector<ShakeRecording> shakes() {
    const std::vector<std::string> paths = shakeFiles(SHAKES_DIR);
    TEST_ASSERT_FALSE_MESSAGE(paths.empty(), "no shake fixtures to roll on");
    std::vector<ShakeRecording> recordings;
    for (const std::string& path : paths) recordings.push_back(loadRecording(path.c_str()));
    return recordings;
}

/** @brief @p results results of @p dice dice of @p shape, rolled one roll after another. */
static ShakenTally rollChained(const DieShape& shape, uint8_t dice, unsigned long results, KickPlan kick) {
    SeededSource source(SEED);
    World world{};
    TEST_ASSERT_TRUE_MESSAGE(restingWorld(world, shape, dice, source, PRODUCT_REST_THRESHOLDS, TIME_LIMIT_S),
                             "the first roll never came to rest");
    const ShakenTally tally = chainRolls(world, shakes(), results / dice, kick, source, PRODUCT_REST_THRESHOLDS, TIME_LIMIT_S);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, tally.undetected, "a recorded shake was never detected");
    // Every roll ends with every die locked, nudged off any wall it came to rest against.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, tally.unsettled, "a roll never came to rest");
    return tally;
}

static double chiSquare(const unsigned long* counts, unsigned long results, uint8_t faces) {
    const double expected = static_cast<double>(results) / faces;
    double statistic = 0;
    for (uint8_t numeral = 1; numeral <= faces; numeral++) {
        const double deviation = counts[numeral] - expected;
        statistic += deviation * deviation / expected;
    }
    return statistic;
}

/** @brief How many standard deviations @p repeats is from 1 in @p faces of @p results. */
static double repeatZ(unsigned long repeats, unsigned long results, uint8_t faces) {
    const double chance = 1.0 / faces;
    return (repeats - results * chance) / sqrt(results * chance * (1 - chance));
}

static void assertFair(const DieShape& shape, uint8_t dice, unsigned long results, double critical) {
    const ShakenTally tally = rollChained(shape, dice, results, PRODUCT_KICK);
    TEST_ASSERT_LESS_THAN_DOUBLE_MESSAGE(critical, chiSquare(tally.counts, tally.results, shape.faceCount),
                                         "results are not uniform");
    TEST_ASSERT_LESS_THAN_DOUBLE_MESSAGE(CRITICAL_REPEAT_Z, fabs(repeatZ(tally.sameAsBefore, tally.results, shape.faceCount)),
                                         "results follow the face the die showed before");
}

/**
 * @brief Launch @p dice dice of @p shape from rest, as a change of dice on the device does, until
 * there are @p results results, and check them with chi-square. A launch draws a uniformly random
 * orientation, so it carries nothing over from the last result: there is no repeat to check.
 */
static void assertLaunchesFair(const DieShape& shape, uint8_t dice, unsigned long results, double critical) {
    SeededSource source(SEED);
    // One world for every roll, as the device keeps one.
    World world{};
    world.shape = &shape;
    unsigned long counts[DIE_MAX_FACES + 1] = {};
    for (unsigned long roll = 0; roll < results / dice; roll++) {
        const RollOutcome outcome = simulateRollIn(world, dice, source, PRODUCT_REST_THRESHOLDS, TIME_LIMIT_S, nullptr);
        TEST_ASSERT_TRUE_MESSAGE(outcome.settled, "a launched roll never came to rest");
        for (uint8_t die = 0; die < dice; die++) counts[outcome.results[die].numeral]++;
    }
    TEST_ASSERT_LESS_THAN_DOUBLE_MESSAGE(critical, chiSquare(counts, results / dice * dice, shape.faceCount),
                                         "launched results are not uniform");
}

void test_the_chi_square_statistic_matches_a_worked_example() {
    // 60 rolls of a D6 as 5, 8, 9, 8, 10, 20: expected 10 each, so the statistic is
    // (25 + 4 + 1 + 4 + 0 + 100) / 10 = 13.4.
    const unsigned long counts[] = {0, 5, 8, 9, 8, 10, 20};
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 13.4, chiSquare(counts, 60, 6));
}

void test_the_repeat_z_matches_a_worked_example() {
    // 3600 D6 results expect 600 repeats with a standard deviation of √(3600 · 1/6 · 5/6) = √500;
    // 700 is 100 / √500 = 4.472 above.
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, 4.472, repeatZ(700, 3600, 6));
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, -4.472, repeatZ(500, 3600, 6));
}

void test_a_recording_must_move_forward_by_less_than_the_detector_window() {
    // A recording whose clock steps back or stalls would replay as a shake that never ends.
    TEST_ASSERT_TRUE(continuesRecording(1000, 11000));
    TEST_ASSERT_TRUE(continuesRecording(UINT32_MAX - 4000, 6000));
    TEST_ASSERT_FALSE(continuesRecording(11000, 11000));
    TEST_ASSERT_FALSE(continuesRecording(11000, 1000));
    TEST_ASSERT_FALSE(continuesRecording(0, SHAKE_WINDOW_US));
}

void test_the_shakes_are_every_csv_in_the_directory_in_name_order() {
    // A failed assertion jumps past the cleanup at the end, so a run starts by clearing what an
    // earlier one left; only two runs of this test at once could collide on the fixed name.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "rattlebox-test-shake-files";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "p2");
    TEST_ASSERT_TRUE_MESSAGE(shakeFiles(dir).empty(), "an empty directory lists nothing, which shakes() refuses");
    // Created out of order, beside a file and a directory that are not takes.
    for (const char* name : {"m5stickc-plus2-shake-hard-1.csv", "cardputer-shake-hard-1.csv", "cardputer-p2-shake-twist-1.csv", "notes.md"}) {
        FILE* file = fopen((dir / name).string().c_str(), "w");
        TEST_ASSERT_NOT_NULL(file);
        fclose(file);
    }
    const std::vector<std::string> paths = shakeFiles(dir);
    TEST_ASSERT_EQUAL_size_t(3, paths.size());
    TEST_ASSERT_EQUAL_STRING((dir / "cardputer-p2-shake-twist-1.csv").generic_string().c_str(), paths[0].c_str());
    TEST_ASSERT_EQUAL_STRING((dir / "cardputer-shake-hard-1.csv").generic_string().c_str(), paths[1].c_str());
    TEST_ASSERT_EQUAL_STRING((dir / "m5stickc-plus2-shake-hard-1.csv").generic_string().c_str(), paths[2].c_str());
    std::filesystem::remove_all(dir);
}

void test_one_d20_at_a_time_is_fair() { assertFair(makeD20(D20_EDGE_MM), 1, D20_RESULTS, CRITICAL_D20); }
void test_one_d6_at_a_time_is_fair() { assertFair(makeD6(D6_EDGE_MM), 1, D6_RESULTS, CRITICAL_D6); }
void test_one_d8_at_a_time_is_fair() { assertFair(makeD8(D8_EDGE_MM), 1, D8_RESULTS, CRITICAL_D8); }
void test_three_d20s_at_a_time_are_fair() { assertFair(makeD20(D20_EDGE_MM), MAX_DICE, D20_RESULTS, CRITICAL_D20); }
void test_three_d6s_at_a_time_are_fair() { assertFair(makeD6(D6_EDGE_MM), MAX_DICE, D6_RESULTS, CRITICAL_D6); }
void test_three_d8s_at_a_time_are_fair() { assertFair(makeD8(D8_EDGE_MM), MAX_DICE, D8_RESULTS, CRITICAL_D8); }

// A change of die type or count launches the new dice from rest (slice 0011), ADR-0009's roll.
void test_one_d20_launched_from_rest_is_fair() { assertLaunchesFair(makeD20(D20_EDGE_MM), 1, D20_RESULTS, CRITICAL_D20); }
void test_one_d6_launched_from_rest_is_fair() { assertLaunchesFair(makeD6(D6_EDGE_MM), 1, D6_RESULTS, CRITICAL_D6); }
void test_one_d8_launched_from_rest_is_fair() { assertLaunchesFair(makeD8(D8_EDGE_MM), 1, D8_RESULTS, CRITICAL_D8); }
void test_three_d20s_launched_from_rest_are_fair() {
    assertLaunchesFair(makeD20(D20_EDGE_MM), MAX_DICE, D20_RESULTS, CRITICAL_D20);
}
void test_three_d6s_launched_from_rest_are_fair() { assertLaunchesFair(makeD6(D6_EDGE_MM), MAX_DICE, D6_RESULTS, CRITICAL_D6); }
void test_three_d8s_launched_from_rest_are_fair() { assertLaunchesFair(makeD8(D8_EDGE_MM), MAX_DICE, D8_RESULTS, CRITICAL_D8); }

void test_a_loaded_d20_fails_chi_square() {
    // Shows the first check can fail: shifting the body off the centre of mass weights one side,
    // and that die lands heavy side down more often.
    DieShape loaded = makeD20(D20_EDGE_MM);
    for (uint8_t vertex = 0; vertex < loaded.vertexCount; vertex++) {
        loaded.vertices[vertex] = loaded.vertices[vertex] + loaded.faces[0].normal * LOAD_MM;
    }
    const ShakenTally tally = rollChained(loaded, 1, D20_RESULTS, PRODUCT_KICK);
    TEST_ASSERT_GREATER_THAN_DOUBLE(CRITICAL_D20, chiSquare(tally.counts, tally.results, loaded.faceCount));
}

void test_a_throw_alone_fails_the_repeat_check() {
    // Shows the second check can fail, on the bias that made it necessary: thrown but not turned,
    // a D6 slid by a gentle shake keeps its face.
    const DieShape d6 = makeD6(D6_EDGE_MM);
    const ShakenTally tally = rollChained(d6, 1, D6_RESULTS, KickPlan{ShakeKick::Throw, 0});
    TEST_ASSERT_GREATER_THAN_DOUBLE(CRITICAL_REPEAT_Z, repeatZ(tally.sameAsBefore, tally.results, d6.faceCount));
}

int main() {
    UNITY_BEGIN();
#ifdef FAIRNESS_THREE_DICE
    RUN_TEST(test_three_d20s_at_a_time_are_fair);
    RUN_TEST(test_three_d6s_at_a_time_are_fair);
    RUN_TEST(test_three_d8s_at_a_time_are_fair);
    RUN_TEST(test_three_d20s_launched_from_rest_are_fair);
    RUN_TEST(test_three_d6s_launched_from_rest_are_fair);
    RUN_TEST(test_three_d8s_launched_from_rest_are_fair);
#else
    RUN_TEST(test_the_chi_square_statistic_matches_a_worked_example);
    RUN_TEST(test_the_repeat_z_matches_a_worked_example);
    RUN_TEST(test_a_recording_must_move_forward_by_less_than_the_detector_window);
    RUN_TEST(test_the_shakes_are_every_csv_in_the_directory_in_name_order);
    RUN_TEST(test_one_d20_at_a_time_is_fair);
    RUN_TEST(test_one_d6_at_a_time_is_fair);
    RUN_TEST(test_one_d8_at_a_time_is_fair);
    RUN_TEST(test_one_d20_launched_from_rest_is_fair);
    RUN_TEST(test_one_d6_launched_from_rest_is_fair);
    RUN_TEST(test_one_d8_launched_from_rest_is_fair);
    RUN_TEST(test_a_loaded_d20_fails_chi_square);
    RUN_TEST(test_a_throw_alone_fails_the_repeat_check);
#endif
    return UNITY_END();
}

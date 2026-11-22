#include <initializer_list>
#include <math.h>
#include <unity.h>
#include "../../src/imu/reading_average.h"
#include "../../src/imu/reading_average.cpp"

// ADR-0014: the reading averaged over 0.1 s, the angle the level follows while dice are in flight.

// ADR-0005's configured rate, and twice its period: the pull must come from the timestamps.
static const uint32_t PERIOD_100HZ_US = 10000;
static const uint32_t PERIOD_50HZ_US = 20000;
static const float US_PER_S_F = 1e6f;
static const Vec3 TILTED{0, 0.5f, 0.866f};
static const Vec3 FLAT{0, 0, 1.0f};
static const float TOLERANCE_G = 1e-5f;
// Ten time constants leave e^−10 of the 0.5 g between the holds, 2.3e−5 g, plus float rounding.
static const float HELD_TOLERANCE_G = 1e-4f;
// Just under the 71-minute wrap of a µs timestamp.
static const uint32_t BEFORE_WRAP_US = UINT32_MAX - PERIOD_100HZ_US / 2;

static ImuSample sampleAt(uint32_t timestampUs, Vec3 accelG) { return {accelG, {0, 0, 0}, timestampUs}; }

static void assertVec(Vec3 expected, Vec3 actual) {
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE_G, expected.x, actual.x);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE_G, expected.y, actual.y);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE_G, expected.z, actual.z);
}

/** @brief Where one sample @p periodUs after a hold at @p from, reading @p to, leaves the average. */
static Vec3 pulled(Vec3 from, Vec3 to, uint32_t periodUs) {
    const float share = 1 - expf(-(periodUs / US_PER_S_F) / READING_AVERAGE_TIME_CONSTANT_S);
    return from + (to - from) * share;
}

void test_the_first_sample_is_the_average() {
    ReadingAverage average;
    average.add(sampleAt(PERIOD_100HZ_US, TILTED));
    assertVec(TILTED, average.meanG());
}

void test_a_sample_pulls_the_average_by_the_time_since_the_last() {
    for (uint32_t periodUs : {PERIOD_100HZ_US, PERIOD_50HZ_US}) {
        ReadingAverage average;
        average.add(sampleAt(0, TILTED));
        average.add(sampleAt(periodUs, FLAT));
        assertVec(pulled(TILTED, FLAT, periodUs), average.meanG());
    }
}

void test_a_hold_of_ten_time_constants_is_its_reading() {
    ReadingAverage average;
    average.add(sampleAt(0, TILTED));
    const uint32_t samples = static_cast<uint32_t>(10 * READING_AVERAGE_TIME_CONSTANT_S * US_PER_S_F / PERIOD_100HZ_US);
    for (uint32_t i = 1; i <= samples; i++) average.add(sampleAt(i * PERIOD_100HZ_US, FLAT));
    // e^−10 of the 0.5 g between them is left.
    const Vec3 mean = average.meanG();
    TEST_ASSERT_FLOAT_WITHIN(HELD_TOLERANCE_G, FLAT.x, mean.x);
    TEST_ASSERT_FLOAT_WITHIN(HELD_TOLERANCE_G, FLAT.y, mean.y);
    TEST_ASSERT_FLOAT_WITHIN(HELD_TOLERANCE_G, FLAT.z, mean.z);
}

void test_the_pull_is_right_across_the_timestamps_wrap() {
    ReadingAverage average;
    average.add(sampleAt(BEFORE_WRAP_US, TILTED));
    average.add(sampleAt(BEFORE_WRAP_US + PERIOD_100HZ_US, FLAT));
    assertVec(pulled(TILTED, FLAT, PERIOD_100HZ_US), average.meanG());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_first_sample_is_the_average);
    RUN_TEST(test_a_sample_pulls_the_average_by_the_time_since_the_last);
    RUN_TEST(test_a_hold_of_ten_time_constants_is_its_reading);
    RUN_TEST(test_the_pull_is_right_across_the_timestamps_wrap);
    return UNITY_END();
}

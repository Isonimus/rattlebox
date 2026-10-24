#include <unity.h>
#include "../../src/imu/shake_detector.h"
#include "../../src/imu/shake_detector.cpp"

// ADR-0005's configured rate; the detector must not depend on it, so some tests use others.
static const uint32_t PERIOD_100HZ_US = 10000;
static const uint32_t PERIOD_50HZ_US = 20000;
static const uint32_t SAMPLES_PER_SECOND_100HZ = 100;
// Readings either side of SHAKE_BUSY_DEVIATION_G off 1 g, in each direction.
static const Vec3 STILL{0, 0, 1.0f};
static const Vec3 HARD_PUSH{0, 0, 1.6f};
static const Vec3 SOFT_PUSH{0, 0, 1.4f};
static const Vec3 DROPPING{0, 0, 0.4f};

static ImuSample sampleAt(uint32_t timestampUs, Vec3 accelG) { return {accelG, {0, 0, 0}, timestampUs}; }

/** @brief Feed @p count samples every @p periodUs from @p startUs; returns the next timestamp. */
static uint32_t feed(ShakeDetector& detector, uint32_t startUs, uint32_t periodUs, uint32_t count, Vec3 accelG) {
    uint32_t timestampUs = startUs;
    for (uint32_t i = 0; i < count; i++) {
        detector.add(sampleAt(timestampUs, accelG));
        timestampUs += periodUs;
    }
    return timestampUs;
}

/** @brief One second at 100 Hz with @p busyCount busy samples first, then still ones. */
static void feedSecond(ShakeDetector& detector, uint32_t busyCount) {
    const uint32_t next = feed(detector, 0, PERIOD_100HZ_US, busyCount, HARD_PUSH);
    feed(detector, next, PERIOD_100HZ_US, SAMPLES_PER_SECOND_100HZ - busyCount, STILL);
}

void test_a_still_device_is_not_shaking() {
    ShakeDetector detector;
    feed(detector, 0, PERIOD_100HZ_US, 3 * SAMPLES_PER_SECOND_100HZ, STILL);
    TEST_ASSERT_FALSE(detector.shaking());
}

void test_a_sustained_shake_is_a_shake() {
    ShakeDetector detector;
    feed(detector, 0, PERIOD_100HZ_US, 2 * SAMPLES_PER_SECOND_100HZ, HARD_PUSH);
    TEST_ASSERT_TRUE(detector.shaking());
}

void test_nothing_is_a_shake_until_a_whole_second_has_been_seen() {
    // Every sample busy, but less than a second of them: at boot, one knock would otherwise count.
    ShakeDetector detector;
    feed(detector, 0, PERIOD_100HZ_US, SAMPLES_PER_SECOND_100HZ, HARD_PUSH);
    TEST_ASSERT_FALSE(detector.shaking());
    detector.add(sampleAt(SAMPLES_PER_SECOND_100HZ * PERIOD_100HZ_US, HARD_PUSH));
    TEST_ASSERT_TRUE(detector.shaking());
}

void test_the_busy_share_must_exceed_the_threshold() {
    // 23 of the 100 samples in the last second is ADR-0005's 0.23 exactly, not over it. The 101st
    // sample, a still one, evicts the first, which a whole-second window must first see.
    ShakeDetector atThreshold;
    feedSecond(atThreshold, 24);
    atThreshold.add(sampleAt(SAMPLES_PER_SECOND_100HZ * PERIOD_100HZ_US, STILL));
    TEST_ASSERT_FALSE(atThreshold.shaking());

    ShakeDetector overThreshold;
    feedSecond(overThreshold, 25);
    overThreshold.add(sampleAt(SAMPLES_PER_SECOND_100HZ * PERIOD_100HZ_US, STILL));
    TEST_ASSERT_TRUE(overThreshold.shaking());
}

void test_busy_means_over_half_a_g_off_one_g_either_way() {
    ShakeDetector soft;
    feed(soft, 0, PERIOD_100HZ_US, 2 * SAMPLES_PER_SECOND_100HZ, SOFT_PUSH);
    TEST_ASSERT_FALSE(soft.shaking());

    ShakeDetector dropping;
    feed(dropping, 0, PERIOD_100HZ_US, 2 * SAMPLES_PER_SECOND_100HZ, DROPPING);
    TEST_ASSERT_TRUE(dropping.shaking());
}

void test_the_window_is_one_second_of_timestamps_not_a_count_of_samples() {
    // At 50 Hz, a second of shaking then a second still: the last second holds no busy sample. A
    // window counting 100 samples at the configured rate would reach back over the shake.
    ShakeDetector detector;
    const uint32_t next = feed(detector, 0, PERIOD_50HZ_US, 50, HARD_PUSH);
    feed(detector, next, PERIOD_50HZ_US, 50, STILL);
    TEST_ASSERT_FALSE(detector.shaking());
}

void test_a_shake_ends_once_the_last_second_is_quiet() {
    ShakeDetector detector;
    const uint32_t next = feed(detector, 0, PERIOD_100HZ_US, 2 * SAMPLES_PER_SECOND_100HZ, HARD_PUSH);
    feed(detector, next, PERIOD_100HZ_US, SAMPLES_PER_SECOND_100HZ, STILL);
    TEST_ASSERT_FALSE(detector.shaking());
}

void test_the_timestamp_wrapping_round_does_not_end_a_shake() {
    // A µs timestamp wraps every 71 minutes; across the wrap, a newer sample has a smaller value.
    ShakeDetector detector;
    const uint32_t beforeWrap = UINT32_MAX - SAMPLES_PER_SECOND_100HZ * PERIOD_100HZ_US;
    feed(detector, beforeWrap, PERIOD_100HZ_US, 2 * SAMPLES_PER_SECOND_100HZ, HARD_PUSH);
    TEST_ASSERT_TRUE(detector.shaking());
}

void test_a_gap_in_the_samples_is_not_a_shake() {
    // The loop stops reading the IMU for seconds while it streams a screen capture. One busy
    // reading after it, the board picked up, is not a busy share of a whole second.
    ShakeDetector detector;
    const uint32_t next = feed(detector, 0, PERIOD_100HZ_US, 2 * SAMPLES_PER_SECOND_100HZ, STILL);
    feed(detector, next + 5 * SHAKE_WINDOW_US, PERIOD_100HZ_US, 3, HARD_PUSH);
    TEST_ASSERT_FALSE(detector.shaking());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_still_device_is_not_shaking);
    RUN_TEST(test_a_sustained_shake_is_a_shake);
    RUN_TEST(test_nothing_is_a_shake_until_a_whole_second_has_been_seen);
    RUN_TEST(test_the_busy_share_must_exceed_the_threshold);
    RUN_TEST(test_busy_means_over_half_a_g_off_one_g_either_way);
    RUN_TEST(test_the_window_is_one_second_of_timestamps_not_a_count_of_samples);
    RUN_TEST(test_a_shake_ends_once_the_last_second_is_quiet);
    RUN_TEST(test_the_timestamp_wrapping_round_does_not_end_a_shake);
    RUN_TEST(test_a_gap_in_the_samples_is_not_a_shake);
    return UNITY_END();
}

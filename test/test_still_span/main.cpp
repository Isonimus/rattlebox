#include <unity.h>
#include "../../src/imu/still_span.h"
#include "../../src/imu/still_span.cpp"

// Slice 0012: the device's latest still moment, whose reading becomes the tray's level.

// ADR-0005's configured rate; the span must not depend on it, so one test uses another.
static const uint32_t PERIOD_100HZ_US = 10000;
static const uint32_t PERIOD_50HZ_US = 20000;
static const uint32_t SAMPLES_PER_SPAN_100HZ = STILL_SPAN_US / PERIOD_100HZ_US;
// Held tilted toward the face, as people hold the device.
static const Vec3 TILTED{0, 0.5f, 0.866f};
static const Vec3 FLAT{0, 0, 1.0f};
// Within, and past, STILL_SPREAD_G of the readings around them. One sample of a span's 26 pulls the
// mean toward itself by 1/26 of its offset, so the one past it is placed past it by more than that.
static const float WITHIN_SPREAD_G = 0.09f;
static const float PAST_SPREAD_G = 0.12f;
static const float TOLERANCE_G = 1e-5f;
// About as long as a screen capture holds the loop on the StickC.
static const uint32_t SAMPLE_GAP_US = 5000000;

static ImuSample sampleAt(uint32_t timestampUs, Vec3 accelG) { return {accelG, {0, 0, 0}, timestampUs}; }

/** @brief Feed @p count samples every @p periodUs from @p startUs; returns the next timestamp. */
static uint32_t feed(StillSpan& span, uint32_t startUs, uint32_t periodUs, uint32_t count, Vec3 accelG) {
    uint32_t timestampUs = startUs;
    for (uint32_t i = 0; i < count; i++) {
        span.add(sampleAt(timestampUs, accelG));
        timestampUs += periodUs;
    }
    return timestampUs;
}

static void assertVecWithin(Vec3 expected, Vec3 actual) {
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE_G, expected.x, actual.x);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE_G, expected.y, actual.y);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE_G, expected.z, actual.z);
}

void test_a_held_angle_becomes_the_latest_still_mean() {
    StillSpan span;
    feed(span, 0, PERIOD_100HZ_US, 2 * SAMPLES_PER_SPAN_100HZ, TILTED);
    TEST_ASSERT_TRUE(span.found());
    assertVecWithin(TILTED, span.latestMeanG());
}

void test_nothing_is_found_until_a_whole_span_has_been_seen() {
    // At boot, a moment's stillness in the middle of handling would otherwise set the level.
    StillSpan span;
    const uint32_t next = feed(span, 0, PERIOD_100HZ_US, SAMPLES_PER_SPAN_100HZ, TILTED);
    TEST_ASSERT_FALSE(span.found());
    span.add(sampleAt(next, TILTED));
    TEST_ASSERT_TRUE(span.found());
}

void test_the_span_is_still_only_while_every_sample_is_within_the_spread() {
    // One sample in the middle of the span decides, either side of STILL_SPREAD_G off the mean.
    const uint32_t half = SAMPLES_PER_SPAN_100HZ / 2;
    StillSpan within;
    uint32_t next = feed(within, 0, PERIOD_100HZ_US, half, FLAT);
    next = feed(within, next, PERIOD_100HZ_US, 1, Vec3{WITHIN_SPREAD_G, 0, 1.0f});
    feed(within, next, PERIOD_100HZ_US, SAMPLES_PER_SPAN_100HZ - half, FLAT);
    TEST_ASSERT_TRUE(within.found());

    StillSpan past;
    next = feed(past, 0, PERIOD_100HZ_US, half, FLAT);
    next = feed(past, next, PERIOD_100HZ_US, 1, Vec3{PAST_SPREAD_G, 0, 1.0f});
    feed(past, next, PERIOD_100HZ_US, SAMPLES_PER_SPAN_100HZ - half, FLAT);
    TEST_ASSERT_FALSE(past.found());
}

void test_motion_keeps_the_last_still_mean() {
    // A shake has no still span, so the hold before it stays the level through it.
    StillSpan span;
    uint32_t next = feed(span, 0, PERIOD_100HZ_US, 2 * SAMPLES_PER_SPAN_100HZ, TILTED);
    for (uint32_t i = 0; i < 4 * SAMPLES_PER_SPAN_100HZ; i++) {
        const Vec3 swing = i % 2 == 0 ? Vec3{1.5f, 0, 1.0f} : Vec3{-1.5f, 0, 1.0f};
        next = feed(span, next, PERIOD_100HZ_US, 1, swing);
    }
    assertVecWithin(TILTED, span.latestMeanG());
}

void test_a_new_hold_replaces_the_old_one() {
    StillSpan span;
    const uint32_t next = feed(span, 0, PERIOD_100HZ_US, 2 * SAMPLES_PER_SPAN_100HZ, TILTED);
    feed(span, next, PERIOD_100HZ_US, 2 * SAMPLES_PER_SPAN_100HZ, FLAT);
    assertVecWithin(FLAT, span.latestMeanG());
}

void test_the_span_is_a_quarter_second_of_timestamps_not_a_count_of_samples() {
    // At 50 Hz, a quarter second swinging then a quarter second flat: the last quarter second is
    // still. A span counting 25 samples at the configured rate would reach back over the swing.
    StillSpan span;
    const uint32_t samplesPerSpan = STILL_SPAN_US / PERIOD_50HZ_US;
    const uint32_t next = feed(span, 0, PERIOD_50HZ_US, samplesPerSpan, Vec3{1.5f, 0, 1.0f});
    feed(span, next, PERIOD_50HZ_US, samplesPerSpan + 1, FLAT);
    TEST_ASSERT_TRUE(span.found());
    assertVecWithin(FLAT, span.latestMeanG());
}

void test_the_timestamp_wrapping_round_keeps_the_span() {
    // A µs timestamp wraps every 71 minutes; across the wrap, a newer sample has a smaller value.
    StillSpan span;
    const uint32_t beforeWrap = UINT32_MAX - SAMPLES_PER_SPAN_100HZ * PERIOD_100HZ_US;
    feed(span, beforeWrap, PERIOD_100HZ_US, 2 * SAMPLES_PER_SPAN_100HZ, TILTED);
    TEST_ASSERT_TRUE(span.found());
    assertVecWithin(TILTED, span.latestMeanG());
}

void test_a_gap_in_the_samples_restarts_the_span() {
    // The loop stops reading the IMU for seconds while it streams a screen capture. The reading
    // after it, mid-handling, is one sample, not a quarter second held still.
    StillSpan span;
    uint32_t next = feed(span, 0, PERIOD_100HZ_US, 2 * SAMPLES_PER_SPAN_100HZ, TILTED);
    next += SAMPLE_GAP_US;
    next = feed(span, next, PERIOD_100HZ_US, 1, Vec3{1.5f, 0, 1.0f});
    assertVecWithin(TILTED, span.latestMeanG());
    // A gap inside the span counts too: two samples a fifth of a second apart are not a span.
    next = feed(span, next + STILL_SPAN_US / 5, PERIOD_100HZ_US, 1, Vec3{1.5f, 0, 1.0f});
    assertVecWithin(TILTED, span.latestMeanG());
    // A whole span of readings after the gap is a still span again.
    feed(span, next, PERIOD_100HZ_US, SAMPLES_PER_SPAN_100HZ + 1, FLAT);
    assertVecWithin(FLAT, span.latestMeanG());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_held_angle_becomes_the_latest_still_mean);
    RUN_TEST(test_nothing_is_found_until_a_whole_span_has_been_seen);
    RUN_TEST(test_the_span_is_still_only_while_every_sample_is_within_the_spread);
    RUN_TEST(test_motion_keeps_the_last_still_mean);
    RUN_TEST(test_a_new_hold_replaces_the_old_one);
    RUN_TEST(test_the_span_is_a_quarter_second_of_timestamps_not_a_count_of_samples);
    RUN_TEST(test_the_timestamp_wrapping_round_keeps_the_span);
    RUN_TEST(test_a_gap_in_the_samples_restarts_the_span);
    return UNITY_END();
}

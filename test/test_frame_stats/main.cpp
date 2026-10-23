#include <unity.h>
#include "../../src/probes/render/frame_stats.h"
#include "../../src/probes/render/frame_stats.cpp"

void test_one_hundred_durations_give_the_50th_95th_and_largest() {
    // 100 down to 1, so sorting matters: by nearest rank the 50th is 50 and the 95th is 95.
    uint32_t durations[100];
    for (uint32_t i = 0; i < 100; i++) durations[i] = 100 - i;
    const PhaseStats stats = summarizeDurations(durations, 100);
    TEST_ASSERT_EQUAL_UINT32(50, stats.medianUs);
    TEST_ASSERT_EQUAL_UINT32(95, stats.p95Us);
    TEST_ASSERT_EQUAL_UINT32(100, stats.maxUs);
}

void test_a_single_slow_frame_shows_only_in_the_maximum() {
    uint32_t durations[600];
    for (uint32_t& duration : durations) duration = 13000;
    durations[300] = 40000;
    const PhaseStats stats = summarizeDurations(durations, 600);
    TEST_ASSERT_EQUAL_UINT32(13000, stats.medianUs);
    TEST_ASSERT_EQUAL_UINT32(13000, stats.p95Us);
    TEST_ASSERT_EQUAL_UINT32(40000, stats.maxUs);
}

void test_the_95th_percentile_rounds_its_rank_up() {
    // 0.95 x 30 = 28.5, so the 95th is the 29th smallest, never one interpolated between.
    uint32_t durations[30];
    for (uint32_t i = 0; i < 30; i++) durations[i] = i + 1;
    TEST_ASSERT_EQUAL_UINT32(29, summarizeDurations(durations, 30).p95Us);
}

void test_one_duration_is_every_figure() {
    uint32_t durations[1] = {777};
    const PhaseStats stats = summarizeDurations(durations, 1);
    TEST_ASSERT_EQUAL_UINT32(777, stats.medianUs);
    TEST_ASSERT_EQUAL_UINT32(777, stats.p95Us);
    TEST_ASSERT_EQUAL_UINT32(777, stats.maxUs);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_one_hundred_durations_give_the_50th_95th_and_largest);
    RUN_TEST(test_a_single_slow_frame_shows_only_in_the_maximum);
    RUN_TEST(test_the_95th_percentile_rounds_its_rank_up);
    RUN_TEST(test_one_duration_is_every_figure);
    return UNITY_END();
}

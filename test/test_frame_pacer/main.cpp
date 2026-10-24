#include <unity.h>
#include "../../src/app/frame_pacer.h"
#include "../../src/app/frame_pacer.cpp"

// The product's 60 fps cadence (slice 0011, ADR-0007).

static const uint32_t DEADLINE_US = 1000000;
static const uint32_t A_LITTLE_US = 2000;

static void assertPace(uint32_t waitUs, uint32_t nextDeadlineUs, bool late, FramePace actual) {
    TEST_ASSERT_EQUAL_UINT32(waitUs, actual.waitUs);
    TEST_ASSERT_EQUAL_UINT32(nextDeadlineUs, actual.nextDeadlineUs);
    TEST_ASSERT_EQUAL(late, actual.late);
}

void test_the_period_is_a_sixtieth_of_a_second() { TEST_ASSERT_EQUAL_UINT32(16667, FRAME_PERIOD_US); }

void test_an_early_frame_waits_for_its_deadline() {
    assertPace(A_LITTLE_US, DEADLINE_US + FRAME_PERIOD_US, false, paceFrame(DEADLINE_US, DEADLINE_US - A_LITTLE_US));
    assertPace(0, DEADLINE_US + FRAME_PERIOD_US, false, paceFrame(DEADLINE_US, DEADLINE_US));
}

void test_a_frame_late_by_less_than_a_period_keeps_the_cadence() {
    assertPace(0, DEADLINE_US + FRAME_PERIOD_US, true, paceFrame(DEADLINE_US, DEADLINE_US + A_LITTLE_US));
    assertPace(0, DEADLINE_US + FRAME_PERIOD_US, true, paceFrame(DEADLINE_US, DEADLINE_US + FRAME_PERIOD_US - 1));
}

void test_a_stall_of_a_period_or_more_starts_the_cadence_over() {
    const uint32_t stalledUntilUs = DEADLINE_US + FRAME_PERIOD_US;
    assertPace(0, stalledUntilUs + FRAME_PERIOD_US, true, paceFrame(DEADLINE_US, stalledUntilUs));
    const uint32_t captureEndUs = DEADLINE_US + 3 * US_PER_S;
    assertPace(0, captureEndUs + FRAME_PERIOD_US, true, paceFrame(DEADLINE_US, captureEndUs));
}

void test_times_compare_across_the_microsecond_clock_wrapping() {
    const uint32_t justBeforeWrapUs = UINT32_MAX - A_LITTLE_US / 2;
    const uint32_t deadlineAfterWrapUs = A_LITTLE_US / 2;
    assertPace(A_LITTLE_US + 1, deadlineAfterWrapUs + FRAME_PERIOD_US, false,
               paceFrame(deadlineAfterWrapUs, justBeforeWrapUs));
    assertPace(0, justBeforeWrapUs + FRAME_PERIOD_US, true, paceFrame(justBeforeWrapUs, deadlineAfterWrapUs));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_period_is_a_sixtieth_of_a_second);
    RUN_TEST(test_an_early_frame_waits_for_its_deadline);
    RUN_TEST(test_a_frame_late_by_less_than_a_period_keeps_the_cadence);
    RUN_TEST(test_a_stall_of_a_period_or_more_starts_the_cadence_over);
    RUN_TEST(test_times_compare_across_the_microsecond_clock_wrapping);
    return UNITY_END();
}

#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "../../src/boot/boot_steps.h"
#include "../../src/boot/boot_steps.cpp"

static const int MAX_EVENTS = 8;

// Everything the runner did, in order: "begin:<name>" when it started a part and
// "started:<name>" when it reported one as up.
static char events[MAX_EVENTS][32];
static int eventCount = 0;

static void recordEvent(const char* kind, const char* name) {
    TEST_ASSERT_LESS_THAN_INT(MAX_EVENTS, eventCount);
    snprintf(events[eventCount++], sizeof(events[0]), "%s:%s", kind, name);
}

class FakePart : public Startable {
public:
    FakePart(const char* name, bool succeeds, const char* reason)
        : _name(name), _succeeds(succeeds), _reason(reason) {}

    bool begin() override {
        recordEvent("begin", _name);
        return _succeeds;
    }
    const char* failureReason() const override { return _reason; }
    const char* description() const override { return _name; }

private:
    const char* _name;
    bool _succeeds;
    const char* _reason;
};

static void recordStarted(const BootStep& step) { recordEvent("started", step.subsystem); }

static void assertEvents(const char* const* expected, int expectedCount) {
    TEST_ASSERT_EQUAL_INT(expectedCount, eventCount);
    for (int i = 0; i < expectedCount; i++) TEST_ASSERT_EQUAL_STRING(expected[i], events[i]);
}

void setUp() { eventCount = 0; }
void tearDown() {}

void test_all_steps_start_in_order_and_each_is_reported() {
    FakePart display("display", true, nullptr);
    FakePart input("input", true, nullptr);
    const BootStep steps[] = {{"display", display}, {"input", input}};

    const BootOutcome outcome = runBootSteps(steps, 2, recordStarted);

    TEST_ASSERT_NULL(outcome.failedStep);
    const char* const expected[] = {"begin:display", "started:display", "begin:input", "started:input"};
    assertEvents(expected, 4);
}

void test_boot_stops_at_the_first_failure_and_names_it() {
    FakePart display("display", true, nullptr);
    FakePart imu("imu", false, "WHO_AM_I read 0x00");
    FakePart power("power", true, nullptr);
    const BootStep steps[] = {{"display", display}, {"imu", imu}, {"power", power}};

    const BootOutcome outcome = runBootSteps(steps, 3, recordStarted);

    TEST_ASSERT_EQUAL_PTR(&steps[1], outcome.failedStep);
    TEST_ASSERT_EQUAL_STRING("WHO_AM_I read 0x00", outcome.failureReason);
    // The failed part is not reported as started, and power is never begun.
    const char* const expected[] = {"begin:display", "started:display", "begin:imu"};
    assertEvents(expected, 3);
}

void test_a_failing_first_step_starts_nothing_else() {
    FakePart display("display", false, "panel init failed");
    FakePart input("input", true, nullptr);
    const BootStep steps[] = {{"display", display}, {"input", input}};

    const BootOutcome outcome = runBootSteps(steps, 2, recordStarted);

    TEST_ASSERT_EQUAL_PTR(&steps[0], outcome.failedStep);
    const char* const expected[] = {"begin:display"};
    assertEvents(expected, 1);
}

void test_a_failure_without_a_reason_still_says_something() {
    FakePart input("input", false, nullptr);
    const BootStep steps[] = {{"input", input}};

    const BootOutcome outcome = runBootSteps(steps, 1, recordStarted);

    TEST_ASSERT_EQUAL_PTR(&steps[0], outcome.failedStep);
    TEST_ASSERT_NOT_NULL(outcome.failureReason);
    TEST_ASSERT_TRUE(strlen(outcome.failureReason) > 0);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_all_steps_start_in_order_and_each_is_reported);
    RUN_TEST(test_boot_stops_at_the_first_failure_and_names_it);
    RUN_TEST(test_a_failing_first_step_starts_nothing_else);
    RUN_TEST(test_a_failure_without_a_reason_still_says_something);
    return UNITY_END();
}

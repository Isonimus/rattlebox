#include <unity.h>
#include "../../src/render/results_band.h"
#include "../../src/render/results_band.cpp"

// The band shown once every die locks (slice 0011): it, not the face, is the result. Before that the
// strip shows the type alone (slice 0017).

static void assertSegment(const char* text, uint8_t die, const BandSegment& segment) {
    TEST_ASSERT_EQUAL_STRING(text, segment.text);
    TEST_ASSERT_EQUAL_UINT8(die, segment.die);
}

void test_one_die_shows_the_type_and_its_result_with_no_total() {
    const uint8_t numerals[] = {17};
    const ResultsBand band = resultsBand("D20", numerals, 1);
    TEST_ASSERT_EQUAL_UINT8(2, band.segmentCount);
    assertSegment("D20", BAND_NO_DIE, band.segments[0]);
    assertSegment("17", 0, band.segments[1]);
}

void test_two_dice_show_each_result_in_its_die_colour_and_the_total() {
    const uint8_t numerals[] = {6, 3};
    const ResultsBand band = resultsBand("D6", numerals, 2);
    TEST_ASSERT_EQUAL_UINT8(4, band.segmentCount);
    assertSegment("D6", BAND_NO_DIE, band.segments[0]);
    assertSegment("6", 0, band.segments[1]);
    assertSegment("3", 1, band.segments[2]);
    assertSegment("= 9", BAND_NO_DIE, band.segments[3]);
}

void test_three_d20s_at_their_highest_fit() {
    const uint8_t numerals[] = {20, 20, 20};
    const ResultsBand band = resultsBand("D20", numerals, MAX_DICE);
    TEST_ASSERT_EQUAL_UINT8(BAND_MAX_SEGMENTS, band.segmentCount);
    assertSegment("20", 2, band.segments[3]);
    assertSegment("= 60", BAND_NO_DIE, band.segments[4]);
}

void test_before_the_dice_lock_the_strip_shows_the_type_alone() {
    const ResultsBand band = typeBand("D6");
    TEST_ASSERT_EQUAL_UINT8(1, band.segmentCount);
    assertSegment("D6", BAND_NO_DIE, band.segments[0]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_one_die_shows_the_type_and_its_result_with_no_total);
    RUN_TEST(test_two_dice_show_each_result_in_its_die_colour_and_the_total);
    RUN_TEST(test_three_d20s_at_their_highest_fit);
    RUN_TEST(test_before_the_dice_lock_the_strip_shows_the_type_alone);
    return UNITY_END();
}

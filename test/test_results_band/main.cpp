#include <string.h>
#include <unity.h>
#include "../../src/geometry/die_numbering.h"
#include "../../src/geometry/die_numbering.cpp"
#include "../../src/geometry/die_shape.h"
#include "../../src/geometry/die_shape.cpp"
#include "../../src/render/results_band.h"
#include "../../src/render/results_band.cpp"
#include "../../src/roll/die_catalog.h"
#include "../../src/roll/die_catalog.cpp"
#include "../../src/roll/roll_band.h"
#include "../../src/roll/roll_band.cpp"

// The band shown once every die locks (slice 0011): it, not the face, is the result. Before that the
// strip shows the type alone (slice 0017). A d% shows its two bodies and their percentile (slice 0035).

static void assertSegment(const char* text, uint8_t die, const BandSegment& segment) {
    TEST_ASSERT_EQUAL_STRING(text, segment.text);
    TEST_ASSERT_EQUAL_UINT8(die, segment.die);
}

void test_one_die_shows_the_type_and_its_result_with_no_total() {
    const uint8_t numerals[] = {17};
    const ResultsBand band = lockedBand(DieType::D20, 0, numerals, 1);
    TEST_ASSERT_EQUAL_UINT8(2, band.segmentCount);
    assertSegment("D20", BAND_NO_DIE, band.segments[0]);
    assertSegment("17", 0, band.segments[1]);
}

void test_two_dice_show_each_result_in_its_die_colour_and_the_total() {
    const uint8_t numerals[] = {6, 3};
    const ResultsBand band = lockedBand(DieType::D6, 0, numerals, 2);
    TEST_ASSERT_EQUAL_UINT8(4, band.segmentCount);
    assertSegment("D6", BAND_NO_DIE, band.segments[0]);
    assertSegment("6", 0, band.segments[1]);
    assertSegment("3", 1, band.segments[2]);
    assertSegment("= 9", BAND_NO_DIE, band.segments[3]);
}

void test_three_d20s_at_their_highest_fit() {
    const uint8_t numerals[] = {20, 20, 20};
    const ResultsBand band = lockedBand(DieType::D20, 0, numerals, MAX_DICE);
    TEST_ASSERT_EQUAL_UINT8(BAND_MAX_SEGMENTS, band.segmentCount);
    assertSegment("20", 2, band.segments[3]);
    assertSegment("= 60", BAND_NO_DIE, band.segments[4]);
}

void test_before_the_dice_lock_the_strip_shows_the_type_alone() {
    const ResultsBand band = choiceBand(DieType::D6, 0);
    TEST_ASSERT_EQUAL_UINT8(1, band.segmentCount);
    assertSegment("D6", BAND_NO_DIE, band.segments[0]);
}

// Slice 0037. A modifier rides on the type, before the roll and in front of its results, because
// the buttons keep it and a modifier nobody can see is a total nobody can account for (ADR-0036).
void test_the_strip_shows_a_modifier_with_its_sign() {
    assertSegment("D6+2", BAND_NO_DIE, choiceBand(DieType::D6, 2).segments[0]);
    assertSegment("D6-2", BAND_NO_DIE, choiceBand(DieType::D6, -2).segments[0]);
    assertSegment("D100+9", BAND_NO_DIE, choiceBand(DieType::D100, MODIFIER_LIMIT).segments[0]);
}

void test_a_modifier_is_added_to_the_total() {
    const uint8_t numerals[] = {6, 3};
    const ResultsBand band = lockedBand(DieType::D6, 2, numerals, 2);
    TEST_ASSERT_EQUAL_UINT8(4, band.segmentCount);
    assertSegment("D6+2", BAND_NO_DIE, band.segments[0]);
    assertSegment("6", 0, band.segments[1]);
    assertSegment("3", 1, band.segments[2]);
    assertSegment("= 11", BAND_NO_DIE, band.segments[3]);
}

// One die and no modifier shows no total, because it would repeat the only result (slice 0017).
// One die with a modifier must show one: 17 and a total of 19 are different numbers.
void test_one_die_with_a_modifier_shows_its_total() {
    const uint8_t numerals[] = {17};
    const ResultsBand band = lockedBand(DieType::D20, 2, numerals, 1);
    TEST_ASSERT_EQUAL_UINT8(3, band.segmentCount);
    assertSegment("D20+2", BAND_NO_DIE, band.segments[0]);
    assertSegment("17", 0, band.segments[1]);
    assertSegment("= 19", BAND_NO_DIE, band.segments[2]);
}

// A modifier can take the total below zero, which an unsigned total showed as four billion.
void test_a_total_can_go_below_zero() {
    const uint8_t numerals[] = {1};
    const ResultsBand band = lockedBand(DieType::D4, -MODIFIER_LIMIT, numerals, 1);
    assertSegment("D4-9", BAND_NO_DIE, band.segments[0]);
    assertSegment("= -8", BAND_NO_DIE, band.segments[2]);
}

// The widest band the product can now show, which ADR-0036 measured at 226 px of the 240: three
// two-digit results, a signed modifier on the type and a three-digit total.
void test_the_widest_modified_band_still_fits() {
    const uint8_t numerals[] = {20, 20, 20};
    const ResultsBand band = lockedBand(DieType::D20, MODIFIER_LIMIT, numerals, MAX_DICE);
    TEST_ASSERT_EQUAL_UINT8(BAND_MAX_SEGMENTS, band.segmentCount);
    assertSegment("D20+9", BAND_NO_DIE, band.segments[0]);
    assertSegment("= 69", BAND_NO_DIE, band.segments[4]);
    int widthPx = BAND_MARGIN_PX;
    for (uint8_t at = 0; at < band.segmentCount; at++) {
        widthPx += static_cast<int>(strlen(band.segments[at].text)) * BAND_FONT_CHAR_WIDTH_PX;
        // What drawResultsBand puts between and around them: a gap after every segment and
        // padding each side of a result's colour chip.
        widthPx += BAND_SEGMENT_GAP_PX;
        if (band.segments[at].die != BAND_NO_DIE) widthPx += 2 * BAND_CHIP_PADDING_PX;
    }
    TEST_ASSERT_LESS_OR_EQUAL(SCREEN_WIDTH_PX, widthPx);
}

// Slice 0035: the two bodies of a d% are one die, so the band shows a percentile and not a sum.
void test_a_percentile_roll_shows_both_bodies_and_their_percentile() {
    // The tens body's 8 is 70 and the units body's 4 is 3, so the roll is 73.
    const uint8_t numerals[] = {8, 4};
    const ResultsBand band = lockedBand(DieType::D100, 0, numerals, 2);
    TEST_ASSERT_EQUAL_UINT8(4, band.segmentCount);
    assertSegment("D100", BAND_NO_DIE, band.segments[0]);
    assertSegment("70", 0, band.segments[1]);
    assertSegment("3", 1, band.segments[2]);
    assertSegment("= 73", BAND_NO_DIE, band.segments[3]);
}

void test_a_percentile_roll_of_the_lowest_face_on_both_bodies_is_a_hundred() {
    // A percentile roll runs 1 to 100, so the outcome that would otherwise be 0 is the top of it.
    const uint8_t numerals[] = {1, 1};
    const ResultsBand band = lockedBand(DieType::D100, 0, numerals, 2);
    assertSegment("00", 0, band.segments[1]);
    assertSegment("0", 1, band.segments[2]);
    assertSegment("= 100", BAND_NO_DIE, band.segments[3]);
}

void test_a_percentile_roll_one_under_a_hundred_is_ninety_nine() {
    const uint8_t numerals[] = {10, 10};
    const ResultsBand band = lockedBand(DieType::D100, 0, numerals, 2);
    assertSegment("90", 0, band.segments[1]);
    assertSegment("9", 1, band.segments[2]);
    assertSegment("= 99", BAND_NO_DIE, band.segments[3]);
}

void test_the_lowest_percentile_above_zero_is_one() {
    const uint8_t numerals[] = {1, 2};
    const ResultsBand band = lockedBand(DieType::D100, 0, numerals, 2);
    assertSegment("= 1", BAND_NO_DIE, band.segments[3]);
}

// Every percentile pair maps to its own outcome: the band can show each of 1 to 100 exactly once,
// which is the claim the fairness test then measures the physics against.
void test_the_hundred_percentile_pairs_cover_one_to_a_hundred_exactly_once() {
    bool seen[PERCENTILE_OUTCOMES + 1] = {};
    for (uint8_t tens = 1; tens <= 10; tens++) {
        for (uint8_t units = 1; units <= 10; units++) {
            const uint8_t numerals[] = {tens, units};
            const unsigned value = dieValue(dieKind(DieType::D100), numerals);
            TEST_ASSERT_GREATER_OR_EQUAL_UINT(1, value);
            TEST_ASSERT_LESS_OR_EQUAL_UINT(PERCENTILE_OUTCOMES, value);
            TEST_ASSERT_FALSE(seen[value]);
            seen[value] = true;
        }
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_one_die_shows_the_type_and_its_result_with_no_total);
    RUN_TEST(test_two_dice_show_each_result_in_its_die_colour_and_the_total);
    RUN_TEST(test_three_d20s_at_their_highest_fit);
    RUN_TEST(test_before_the_dice_lock_the_strip_shows_the_type_alone);
    RUN_TEST(test_the_strip_shows_a_modifier_with_its_sign);
    RUN_TEST(test_a_modifier_is_added_to_the_total);
    RUN_TEST(test_one_die_with_a_modifier_shows_its_total);
    RUN_TEST(test_a_total_can_go_below_zero);
    RUN_TEST(test_the_widest_modified_band_still_fits);
    RUN_TEST(test_a_percentile_roll_shows_both_bodies_and_their_percentile);
    RUN_TEST(test_a_percentile_roll_of_the_lowest_face_on_both_bodies_is_a_hundred);
    RUN_TEST(test_a_percentile_roll_one_under_a_hundred_is_ninety_nine);
    RUN_TEST(test_the_lowest_percentile_above_zero_is_one);
    RUN_TEST(test_the_hundred_percentile_pairs_cover_one_to_a_hundred_exactly_once);
    return UNITY_END();
}

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
#include "../../src/roll/dice_notation.h"
#include "../../src/roll/dice_notation.cpp"
#include "../../src/roll/expression_entry.h"
#include "../../src/roll/expression_entry.cpp"

// The band is the entry field (ADR-0036, slice 0037): typing opens it, Backspace to empty closes
// it and hands the key back to the controls, and a refused expression stays on screen.

static ExpressionEntry typed(const char* text) {
    ExpressionEntry entry = emptyEntry();
    for (const char* character = text; *character != '\0'; character++) {
        TEST_ASSERT_TRUE_MESSAGE(typeCharacter(entry, *character), text);
    }
    return entry;
}

void test_a_fresh_field_is_closed_and_empty() {
    const ExpressionEntry entry = emptyEntry();
    TEST_ASSERT_FALSE(entry.open);
    TEST_ASSERT_EQUAL_UINT8(0, entry.length);
    TEST_ASSERT_EQUAL_STRING("", entry.text);
    TEST_ASSERT_EQUAL_STRING("", entry.reason);
}

void test_typing_opens_the_field_and_appends() {
    const ExpressionEntry entry = typed("3d6+2");
    TEST_ASSERT_TRUE(entry.open);
    TEST_ASSERT_EQUAL_UINT8(5, entry.length);
    TEST_ASSERT_EQUAL_STRING("3d6+2", entry.text);
}

// A key the grammar cannot use never reaches the field, so the field cannot hold an input whose
// only outcome is a refusal the player cannot act on.
void test_a_character_the_grammar_cannot_use_is_not_taken() {
    ExpressionEntry entry = emptyEntry();
    TEST_ASSERT_FALSE(typeCharacter(entry, 'k'));
    TEST_ASSERT_FALSE(typeCharacter(entry, ' '));
    TEST_ASSERT_FALSE(entry.open);
    TEST_ASSERT_EQUAL_STRING("", entry.text);
}

void test_a_full_field_keeps_what_it_holds() {
    ExpressionEntry entry = emptyEntry();
    for (uint8_t at = 0; at < EXPRESSION_MAX_CHARS; at++) TEST_ASSERT_TRUE(typeCharacter(entry, '1'));
    TEST_ASSERT_EQUAL_UINT8(EXPRESSION_MAX_CHARS, entry.length);
    TEST_ASSERT_FALSE(typeCharacter(entry, '1'));
    TEST_ASSERT_EQUAL_UINT8(EXPRESSION_MAX_CHARS, entry.length);
    TEST_ASSERT_EQUAL_UINT8(EXPRESSION_MAX_CHARS, strlen(entry.text));
}

void test_backspace_deletes_and_an_empty_field_closes() {
    ExpressionEntry entry = typed("3d6");
    TEST_ASSERT_TRUE(backspaceEntry(entry));
    TEST_ASSERT_EQUAL_STRING("3d", entry.text);
    TEST_ASSERT_TRUE(backspaceEntry(entry));
    TEST_ASSERT_EQUAL_STRING("3", entry.text);
    // The last character takes the field with it: Backspace is SECONDARY again from here.
    TEST_ASSERT_FALSE(backspaceEntry(entry));
    TEST_ASSERT_FALSE(entry.open);
    TEST_ASSERT_EQUAL_UINT8(0, entry.length);
    TEST_ASSERT_EQUAL_STRING("", entry.text);
}

void test_an_accepted_expression_closes_and_clears_the_field() {
    ExpressionEntry entry = typed("3d6+2");
    const ParsedNotation parsed = submitEntry(entry);
    TEST_ASSERT_TRUE(parsed.accepted);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DieType::D6), static_cast<uint8_t>(parsed.choice.type));
    TEST_ASSERT_EQUAL_UINT8(3, parsed.choice.count);
    TEST_ASSERT_EQUAL_INT8(2, parsed.choice.modifier);
    TEST_ASSERT_FALSE(entry.open);
    TEST_ASSERT_EQUAL_STRING("", entry.text);
}

// The Definition of Done scenario about a refusal: nothing is thrown, the reason is on screen, and
// what was typed is still there to be corrected.
void test_a_refused_expression_stays_on_screen_with_its_reason() {
    ExpressionEntry entry = typed("4d6");
    const ParsedNotation parsed = submitEntry(entry);
    TEST_ASSERT_FALSE(parsed.accepted);
    TEST_ASSERT_EQUAL_STRING(NOTATION_REASON_TOO_MANY_DICE, entry.reason);
    TEST_ASSERT_TRUE(entry.open);
    TEST_ASSERT_EQUAL_STRING("4d6", entry.text);
}

void test_answering_a_refusal_clears_it() {
    ExpressionEntry refused = typed("4d6");
    submitEntry(refused);
    ExpressionEntry corrected = refused;
    TEST_ASSERT_TRUE(backspaceEntry(corrected));
    TEST_ASSERT_EQUAL_STRING("", corrected.reason);
    TEST_ASSERT_EQUAL_STRING("4d", corrected.text);

    ExpressionEntry extended = refused;
    TEST_ASSERT_TRUE(typeCharacter(extended, '-'));
    TEST_ASSERT_EQUAL_STRING("", extended.reason);
    TEST_ASSERT_EQUAL_STRING("4d6-", extended.text);
}

void test_the_band_shows_the_text_and_a_cursor() {
    const ExpressionEntry entry = typed("3d6");
    const ResultsBand band = entryBand(entry);
    TEST_ASSERT_EQUAL_UINT8(1, band.segmentCount);
    TEST_ASSERT_EQUAL_STRING("3d6_", band.segments[0].text);
    TEST_ASSERT_EQUAL_UINT8(BAND_NO_DIE, band.segments[0].die);
}

void test_the_band_shows_a_refusal_instead_of_the_text() {
    ExpressionEntry entry = typed("4d6");
    submitEntry(entry);
    const ResultsBand band = entryBand(entry);
    TEST_ASSERT_EQUAL_STRING(NOTATION_REASON_TOO_MANY_DICE, band.segments[0].text);
}

// messageBand aborts on text it cannot draw, so a full field coming back whole is a field the
// band really shows, cursor included.
void test_the_fullest_field_still_fits_the_band() {
    ExpressionEntry entry = emptyEntry();
    for (uint8_t at = 0; at < EXPRESSION_MAX_CHARS; at++) typeCharacter(entry, '9');
    const ResultsBand band = entryBand(entry);
    TEST_ASSERT_EQUAL_UINT8(EXPRESSION_MAX_CHARS + 1, strlen(band.segments[0].text));
    TEST_ASSERT_EQUAL_CHAR(ENTRY_CURSOR, band.segments[0].text[EXPRESSION_MAX_CHARS]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_fresh_field_is_closed_and_empty);
    RUN_TEST(test_typing_opens_the_field_and_appends);
    RUN_TEST(test_a_character_the_grammar_cannot_use_is_not_taken);
    RUN_TEST(test_a_full_field_keeps_what_it_holds);
    RUN_TEST(test_backspace_deletes_and_an_empty_field_closes);
    RUN_TEST(test_an_accepted_expression_closes_and_clears_the_field);
    RUN_TEST(test_a_refused_expression_stays_on_screen_with_its_reason);
    RUN_TEST(test_answering_a_refusal_clears_it);
    RUN_TEST(test_the_band_shows_the_text_and_a_cursor);
    RUN_TEST(test_the_band_shows_a_refusal_instead_of_the_text);
    RUN_TEST(test_the_fullest_field_still_fits_the_band);
    return UNITY_END();
}

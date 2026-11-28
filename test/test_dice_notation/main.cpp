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

// Dice notation, bounded by the tray rather than by the notation (ADR-0036, slice 0037): the
// grammar is `[N] d M [+K | -K]`, an expression the tray cannot throw is refused rather than
// reduced to one it can, and every reason fits the band.

static void assertAccepted(const char* text, DieType type, uint8_t count, int8_t modifier) {
    const ParsedNotation parsed = parseNotation(text);
    TEST_ASSERT_TRUE_MESSAGE(parsed.accepted, text);
    TEST_ASSERT_EQUAL_STRING("", parsed.reason);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(type), static_cast<uint8_t>(parsed.choice.type));
    TEST_ASSERT_EQUAL_UINT8(count, parsed.choice.count);
    TEST_ASSERT_EQUAL_INT8(modifier, parsed.choice.modifier);
}

static void assertRefused(const char* text, const char* reason) {
    const ParsedNotation parsed = parseNotation(text);
    TEST_ASSERT_FALSE_MESSAGE(parsed.accepted, text);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(reason, parsed.reason, text);
    // Nothing is thrown for a refused expression, and a count of 0 is a choice no roll can use.
    TEST_ASSERT_EQUAL_UINT8(0, parsed.choice.count);
}

// Every die in the catalog is reachable by typing the number its own name spells, so the notation
// and the catalog cannot part company (ADR-0034).
void test_every_type_is_typed_by_the_number_in_its_name() {
    for (uint8_t index = 0; index < DIE_TYPE_COUNT; index++) {
        const DieType type = static_cast<DieType>(index);
        char expression[EXPRESSION_MAX_CHARS + 1];
        snprintf(expression, sizeof expression, "1%s", dieTypeName(type));
        // The name is "D20"; typed, the d is lower case.
        expression[1] = 'd';
        assertAccepted(expression, type, 1, 0);
    }
}

void test_an_uppercase_d_is_the_same_expression() {
    assertAccepted("3D6", DieType::D6, 3, 0);
}

void test_a_count_left_out_means_one_die() {
    assertAccepted("d20", DieType::D20, 1, 0);
    assertAccepted("d100", DieType::D100, 1, 0);
}

void test_the_count_reaches_what_the_tray_holds_and_stops() {
    assertAccepted("1d6", DieType::D6, 1, 0);
    assertAccepted("3d6", DieType::D6, 3, 0);
    assertRefused("4d6", NOTATION_REASON_TOO_MANY_DICE);
    assertRefused("9d6", NOTATION_REASON_TOO_MANY_DICE);
    // A second count digit is refused rather than read as the first: "33d6" asked for 33 dice.
    assertRefused("33d6", NOTATION_REASON_TOO_MANY_DICE);
    assertRefused("0d6", NOTATION_REASON_NO_DICE);
}

// The d% is two bodies of the three the tray holds, so two of it is four bodies (ADR-0034). Its
// refusal is its own: "the tray holds 3" would be true of bodies and false of dice.
void test_a_percentile_roll_is_one_die() {
    assertAccepted("1d100", DieType::D100, 1, 0);
    assertRefused("2d100", NOTATION_REASON_TWO_BODIES);
    assertRefused("3d100", NOTATION_REASON_TWO_BODIES);
}

void test_a_modifier_is_added_or_taken() {
    assertAccepted("3d6+2", DieType::D6, 3, 2);
    assertAccepted("3d6-2", DieType::D6, 3, -2);
    assertAccepted("1d20+9", DieType::D20, 1, MODIFIER_LIMIT);
    assertAccepted("1d20-9", DieType::D20, 1, -MODIFIER_LIMIT);
    // A modifier of nothing is an expression a player can type on the way to another.
    assertAccepted("1d20+0", DieType::D20, 1, 0);
}

// The band has room for one digit, so a second is refused and not truncated to the first
// (ADR-0036): "3d6+12" rolled as "3d6+1" is a total the band cannot account for.
void test_a_modifier_the_band_cannot_show_is_refused() {
    assertRefused("3d6+10", NOTATION_REASON_MODIFIER);
    assertRefused("3d6+12", NOTATION_REASON_MODIFIER);
    assertRefused("3d6-99", NOTATION_REASON_MODIFIER);
}

void test_a_die_the_tray_does_not_have_is_refused() {
    assertRefused("1d7", NOTATION_REASON_NO_SUCH_DIE);
    assertRefused("1d0", NOTATION_REASON_NO_SUCH_DIE);
    assertRefused("1d200", NOTATION_REASON_NO_SUCH_DIE);
    // Leading zeros spell a die the catalog does not name, and guessing at what was meant is how
    // a refusal becomes a different roll.
    assertRefused("1d06", NOTATION_REASON_NO_SUCH_DIE);
    // A fourth faces digit cannot name any die, so it is refused before the name is built.
    assertRefused("1d1000", NOTATION_REASON_NO_SUCH_DIE);
}

void test_what_is_not_an_expression_is_refused() {
    assertRefused("", NOTATION_REASON_SHAPE);
    assertRefused("d", NOTATION_REASON_SHAPE);
    assertRefused("3", NOTATION_REASON_SHAPE);
    assertRefused("3d", NOTATION_REASON_SHAPE);
    assertRefused("+2", NOTATION_REASON_SHAPE);
    assertRefused("3d6+", NOTATION_REASON_SHAPE);
    assertRefused("3d6-", NOTATION_REASON_SHAPE);
    assertRefused("3d6+2+2", NOTATION_REASON_SHAPE);
    assertRefused("3d6 ", NOTATION_REASON_SHAPE);
    assertRefused("d6d6", NOTATION_REASON_SHAPE);
}

// The Definition of Done scenario about the reasons: a refusal the band cannot draw is a refusal
// the player cannot read, so every reason goes through the band that will show it (ADR-0036).
void test_every_reason_fits_the_band() {
    const char* reasons[] = {NOTATION_REASON_SHAPE,          NOTATION_REASON_NO_SUCH_DIE,
                             NOTATION_REASON_NO_DICE,        NOTATION_REASON_TOO_MANY_DICE,
                             NOTATION_REASON_TWO_BODIES,     NOTATION_REASON_MODIFIER};
    for (const char* reason : reasons) {
        TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(BAND_MESSAGE_MAX_CHARS, strlen(reason), reason);
        // messageBand aborts on text it cannot draw, and would truncate text a segment cannot
        // hold, so a reason that comes back whole is a reason the band really shows.
        const ResultsBand band = messageBand(reason);
        TEST_ASSERT_EQUAL_UINT8(1, band.segmentCount);
        TEST_ASSERT_EQUAL_STRING(reason, band.segments[0].text);
    }
}

// The field takes the grammar's characters and nothing else, so it can never hold an input whose
// only possible outcome is a refusal (ADR-0036).
void test_only_the_grammars_own_characters_are_notation() {
    const char* notation = "0123456789dD+-";
    for (const char* character = notation; *character != '\0'; character++) {
        TEST_ASSERT_TRUE_MESSAGE(isNotationCharacter(*character), character);
    }
    const char* rejected = "ab ce\tkhl=*/.,()";
    for (const char* character = rejected; *character != '\0'; character++) {
        TEST_ASSERT_FALSE_MESSAGE(isNotationCharacter(*character), character);
    }
    TEST_ASSERT_FALSE(isNotationCharacter('\0'));
}

// The longest expression the grammar accepts has to be typeable in the field it is typed into.
void test_the_longest_valid_expression_fits_the_field() {
    const char* longest = "1d100+9";
    TEST_ASSERT_LESS_OR_EQUAL(EXPRESSION_MAX_CHARS, strlen(longest));
    assertAccepted(longest, DieType::D100, 1, MODIFIER_LIMIT);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_every_type_is_typed_by_the_number_in_its_name);
    RUN_TEST(test_an_uppercase_d_is_the_same_expression);
    RUN_TEST(test_a_count_left_out_means_one_die);
    RUN_TEST(test_the_count_reaches_what_the_tray_holds_and_stops);
    RUN_TEST(test_a_percentile_roll_is_one_die);
    RUN_TEST(test_a_modifier_is_added_or_taken);
    RUN_TEST(test_a_modifier_the_band_cannot_show_is_refused);
    RUN_TEST(test_a_die_the_tray_does_not_have_is_refused);
    RUN_TEST(test_what_is_not_an_expression_is_refused);
    RUN_TEST(test_every_reason_fits_the_band);
    RUN_TEST(test_only_the_grammars_own_characters_are_notation);
    RUN_TEST(test_the_longest_valid_expression_fits_the_field);
    return UNITY_END();
}

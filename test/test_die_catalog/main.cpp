#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/geometry/die_numbering.h"
#include "../../src/geometry/die_numbering.cpp"
#include "../../src/geometry/die_shape.h"
#include "../../src/geometry/die_shape.cpp"
#include "../../src/roll/die_catalog.h"
#include "../../src/roll/die_catalog.cpp"

// The catalog is the one list of the dice the product rolls, so these walk every row rather than
// naming the types one by one: a die added to the enum without a row of its own fails here, and so
// does a row whose name and body disagree.

// "D20" without its leading D is how many outcomes the row promises. For a one-body die that is
// its faces; for the d% it is 100, which its two ten-faced bodies reach together (ADR-0034).
static unsigned outcomesPromisedByName(const char* name) {
    TEST_ASSERT_EQUAL_CHAR('D', name[0]);
    return static_cast<unsigned>(atoi(name + 1));
}

static unsigned outcomesOf(const DieKind& kind) {
    unsigned outcomes = 1;
    for (uint8_t body = 0; body < kind.bodiesPerRoll; body++) outcomes *= kind.shape.faceCount;
    return outcomes;
}

// Every way one die's bodies can land, walked as an odometer over their numerals, each 1 to faces.
static bool nextNumerals(uint8_t* numerals, uint8_t bodies, uint8_t faces) {
    for (uint8_t body = bodies; body > 0; body--) {
        if (numerals[body - 1] < faces) {
            numerals[body - 1]++;
            return true;
        }
        numerals[body - 1] = 1;
    }
    return false;
}

void test_every_type_has_a_row_whose_bodies_reach_the_outcomes_its_name_promises() {
    for (uint8_t index = 0; index < DIE_TYPE_COUNT; index++) {
        const DieKind& kind = dieKind(static_cast<DieType>(index));
        TEST_ASSERT_NOT_NULL(kind.name);
        TEST_ASSERT_EQUAL_UINT(outcomesPromisedByName(kind.name), outcomesOf(kind));
        // A body of no size would roll but never settle; every die is built at its product edge.
        TEST_ASSERT_TRUE(kind.shape.circumradius > 0);
    }
}

// Slice 0035. A die throws at least one body and never more than the per-body declaration holds,
// and a roll of as many of them as the controls allow still fits the tray.
void test_every_type_throws_a_countable_number_of_bodies_that_fits_the_tray() {
    for (uint8_t index = 0; index < DIE_TYPE_COUNT; index++) {
        const DieType type = static_cast<DieType>(index);
        const DieKind& kind = dieKind(type);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(1, kind.bodiesPerRoll);
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(MAX_BODIES_PER_DIE, kind.bodiesPerRoll);
        // The host probe's tallies are arrays of MAX_DIE_OUTCOMES + 1, indexed by dieValue, so a
        // row that reached further would write past them: silent stack corruption on the host
        // rather than a failing test. A die of 11 faces thrown two at a time would do it.
        TEST_ASSERT_LESS_OR_EQUAL_UINT(MAX_DIE_OUTCOMES, outcomesOf(kind));
        // A type no one could roll even once would be unreachable through the controls.
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(1, maxDiceOf(type));
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(MAX_DICE, maxDiceOf(type) * kind.bodiesPerRoll);
    }
}

// Slice 0035. The bodies of one die repeat their numbering, so body 3 of a two-body die is a tens
// body again; this is what lets the renderer and the band label a world body from its index alone.
void test_a_bodys_numbering_repeats_with_the_dice_it_belongs_to() {
    for (uint8_t index = 0; index < DIE_TYPE_COUNT; index++) {
        const DieKind& kind = dieKind(static_cast<DieType>(index));
        for (uint8_t body = 0; body < MAX_DICE; body++) {
            TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(kind.numbering[body % kind.bodiesPerRoll]),
                                    static_cast<uint8_t>(bodyNumbering(kind, body)));
        }
    }
}

// Slice 0035, and the claim the fairness test then measures the physics against: whatever a type's
// bodies do, the values they produce are exactly 1 to its outcome count, each reachable one way.
// A numbering that lost a value or doubled one would make a fair die read unfairly.
void test_every_types_bodies_cover_its_outcomes_exactly_once() {
    for (uint8_t index = 0; index < DIE_TYPE_COUNT; index++) {
        const DieKind& kind = dieKind(static_cast<DieType>(index));
        const unsigned outcomes = outcomesOf(kind);
        bool* seen = static_cast<bool*>(calloc(outcomes + 1, sizeof(bool)));
        TEST_ASSERT_NOT_NULL(seen);
        uint8_t numerals[MAX_BODIES_PER_DIE];
        for (uint8_t body = 0; body < MAX_BODIES_PER_DIE; body++) numerals[body] = 1;
        unsigned counted = 0;
        do {
            const unsigned value = dieValue(kind, numerals);
            TEST_ASSERT_GREATER_OR_EQUAL_UINT(1, value);
            TEST_ASSERT_LESS_OR_EQUAL_UINT(outcomes, value);
            TEST_ASSERT_FALSE(seen[value]);
            seen[value] = true;
            counted++;
        } while (nextNumerals(numerals, kind.bodiesPerRoll, kind.shape.faceCount));
        TEST_ASSERT_EQUAL_UINT(outcomes, counted);
        free(seen);
    }
}

// Slice 0035. A label is what the player reads off the face, so it must fit the buffer the
// renderer and the band size from, and no two faces of one body may read alike.
void test_every_bodys_faces_carry_labels_that_fit_and_do_not_repeat() {
    for (uint8_t index = 0; index < DIE_TYPE_COUNT; index++) {
        const DieKind& kind = dieKind(static_cast<DieType>(index));
        for (uint8_t body = 0; body < kind.bodiesPerRoll; body++) {
            char labels[DIE_MAX_FACES][NUMERAL_LABEL_MAX_CHARS + 1];
            for (uint8_t face = 0; face < kind.shape.faceCount; face++) {
                numeralLabel(bodyNumbering(kind, body), kind.shape.faces[face].numeral, labels[face],
                             sizeof labels[face]);
                TEST_ASSERT_LESS_OR_EQUAL_UINT(NUMERAL_LABEL_MAX_CHARS, strlen(labels[face]));
                for (uint8_t earlier = 0; earlier < face; earlier++) {
                    TEST_ASSERT_NOT_EQUAL(0, strcmp(labels[earlier], labels[face]));
                }
            }
        }
    }
}

void test_no_two_types_share_a_name() {
    for (uint8_t a = 0; a < DIE_TYPE_COUNT; a++) {
        for (uint8_t b = a + 1; b < DIE_TYPE_COUNT; b++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(dieTypeName(static_cast<DieType>(a)), dieTypeName(static_cast<DieType>(b))));
        }
    }
}

void test_a_name_fits_the_width_the_name_buffers_are_sized_from() {
    for (uint8_t index = 0; index < DIE_TYPE_COUNT; index++) {
        TEST_ASSERT_LESS_OR_EQUAL_UINT(DIE_NAME_MAX_CHARS, strlen(dieTypeName(static_cast<DieType>(index))));
    }
}

void test_parsing_a_name_gives_back_the_type_that_carries_it() {
    for (uint8_t index = 0; index < DIE_TYPE_COUNT; index++) {
        const DieType expected = static_cast<DieType>(index);
        DieType parsed = static_cast<DieType>(DIE_TYPE_COUNT);
        TEST_ASSERT_TRUE(parseDieType(dieTypeName(expected), parsed));
        TEST_ASSERT_EQUAL_UINT8(index, static_cast<uint8_t>(parsed));
    }
}

void test_parsing_an_unknown_name_fails_and_leaves_the_type_alone() {
    // Lower case and a die the product does not roll yet: neither may pick a type by accident.
    const char* unknown[] = {"d6", "D7", "", "D20x", "20"};
    for (const char* name : unknown) {
        DieType parsed = DieType::D8;
        TEST_ASSERT_FALSE(parseDieType(name, parsed));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DieType::D8), static_cast<uint8_t>(parsed));
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_every_type_has_a_row_whose_bodies_reach_the_outcomes_its_name_promises);
    RUN_TEST(test_every_type_throws_a_countable_number_of_bodies_that_fits_the_tray);
    RUN_TEST(test_a_bodys_numbering_repeats_with_the_dice_it_belongs_to);
    RUN_TEST(test_every_types_bodies_cover_its_outcomes_exactly_once);
    RUN_TEST(test_every_bodys_faces_carry_labels_that_fit_and_do_not_repeat);
    RUN_TEST(test_no_two_types_share_a_name);
    RUN_TEST(test_a_name_fits_the_width_the_name_buffers_are_sized_from);
    RUN_TEST(test_parsing_a_name_gives_back_the_type_that_carries_it);
    RUN_TEST(test_parsing_an_unknown_name_fails_and_leaves_the_type_alone);
    return UNITY_END();
}

#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/geometry/die_shape.h"
#include "../../src/geometry/die_shape.cpp"
#include "../../src/roll/die_catalog.h"
#include "../../src/roll/die_catalog.cpp"

// The catalog is the one list of the dice the product rolls, so these walk every row rather than
// naming the types one by one: a die added to the enum without a row of its own fails here, and so
// does a row whose name and body disagree.

// "D20" without its leading D is the number of faces the row promises.
static uint8_t facesPromisedByName(const char* name) {
    TEST_ASSERT_EQUAL_CHAR('D', name[0]);
    return static_cast<uint8_t>(atoi(name + 1));
}

void test_every_type_has_a_row_whose_body_has_the_faces_its_name_promises() {
    for (uint8_t index = 0; index < DIE_TYPE_COUNT; index++) {
        const DieKind& kind = dieKind(static_cast<DieType>(index));
        TEST_ASSERT_NOT_NULL(kind.name);
        TEST_ASSERT_EQUAL_UINT8(facesPromisedByName(kind.name), kind.shape.faceCount);
        // A body of no size would roll but never settle; every die is built at its product edge.
        TEST_ASSERT_TRUE(kind.shape.circumradius > 0);
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
    RUN_TEST(test_every_type_has_a_row_whose_body_has_the_faces_its_name_promises);
    RUN_TEST(test_no_two_types_share_a_name);
    RUN_TEST(test_a_name_fits_the_width_the_name_buffers_are_sized_from);
    RUN_TEST(test_parsing_a_name_gives_back_the_type_that_carries_it);
    RUN_TEST(test_parsing_an_unknown_name_fails_and_leaves_the_type_alone);
    return UNITY_END();
}

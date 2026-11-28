#include <stdio.h>
#include <string.h>
#include <unity.h>
#include "../../src/hal/boards/cardputer/tca8418_keymap.h"
#include "../../src/hal/boards/cardputer/tca8418_keymap.cpp"

// Facts about the hardware, stated independently of the decoder's constants.
static const uint8_t PRESSED_BIT = 0x80;
static const uint8_t WIRED_ROWS = 7;
static const uint8_t WIRED_COLS = 8;
static const uint8_t LAYOUT_ROWS = 4;
static const uint8_t LAYOUT_COLS = 14;
static const uint8_t FIRST_GPI_EVENT = 97;  // TCA8418 datasheet: GPI events start at 97

static uint8_t keyNumber(uint8_t matrixRow, uint8_t matrixCol) {
    return matrixRow * 10 + matrixCol + 1;
}

void test_empty_fifo_entry_is_rejected() {
    CardputerKeyEvent event;
    TEST_ASSERT_FALSE(decodeTca8418Event(0x00, event));
    TEST_ASSERT_FALSE(decodeTca8418Event(PRESSED_BIT, event));
}

void test_gpi_event_is_rejected() {
    CardputerKeyEvent event;
    TEST_ASSERT_FALSE(decodeTca8418Event(FIRST_GPI_EVENT | PRESSED_BIT, event));
}

void test_unwired_matrix_column_is_rejected() {
    CardputerKeyEvent event;
    TEST_ASSERT_FALSE(decodeTca8418Event(keyNumber(0, WIRED_COLS) | PRESSED_BIT, event));
}

void test_unwired_matrix_row_is_rejected() {
    CardputerKeyEvent event;
    TEST_ASSERT_FALSE(decodeTca8418Event(keyNumber(WIRED_ROWS, 0) | PRESSED_BIT, event));
}

void test_pressed_bit_sets_pressed_and_clear_bit_means_released() {
    CardputerKeyEvent event;
    TEST_ASSERT_TRUE(decodeTca8418Event(keyNumber(3, 2) | PRESSED_BIT, event));
    TEST_ASSERT_TRUE(event.pressed);
    TEST_ASSERT_TRUE(decodeTca8418Event(keyNumber(3, 2), event));
    TEST_ASSERT_FALSE(event.pressed);
}

void test_every_wired_key_lands_on_a_distinct_printed_key() {
    bool seen[LAYOUT_ROWS][LAYOUT_COLS] = {};
    for (uint8_t row = 0; row < WIRED_ROWS; row++) {
        for (uint8_t col = 0; col < WIRED_COLS; col++) {
            CardputerKeyEvent event;
            TEST_ASSERT_TRUE(decodeTca8418Event(keyNumber(row, col) | PRESSED_BIT, event));
            TEST_ASSERT_LESS_THAN_UINT8(LAYOUT_ROWS, event.key.row);
            TEST_ASSERT_LESS_THAN_UINT8(LAYOUT_COLS, event.key.col);
            TEST_ASSERT_FALSE_MESSAGE(seen[event.key.row][event.key.col], "two matrix keys share a printed key");
            seen[event.key.row][event.key.col] = true;
        }
    }
}

// Enter and Backspace are the keys the game binds. Their matrix positions come
// from M5Stack's reference driver and must be confirmed on the device.
void test_enter_is_matrix_row_6_col_6() {
    CardputerKeyEvent event;
    TEST_ASSERT_TRUE(decodeTca8418Event(keyNumber(6, 6) | PRESSED_BIT, event));
    TEST_ASSERT_TRUE(event.key == cardputer_keys::ENTER);
}

void test_backspace_is_matrix_row_6_col_4() {
    CardputerKeyEvent event;
    TEST_ASSERT_TRUE(decodeTca8418Event(keyNumber(6, 4) | PRESSED_BIT, event));
    TEST_ASSERT_TRUE(event.key == cardputer_keys::BACKSPACE);
}

// The character table (ADR-0036, slice 0037). Written out as the whole printed layout, a row at a
// time, with '.' for a key that types nothing: a golden layout catches a key that starts typing
// something as surely as one that stops, and the grammar's characters are the only ones wanted.
static const char NOTHING_TYPED = '.';
static const char* UNSHIFTED_LAYOUT[LAYOUT_ROWS] = {
    ".1234567890-..",
    "..............",
    "....d.........",
    "..............",
};
static const char* SHIFTED_LAYOUT[LAYOUT_ROWS] = {
    "............+.",
    "..............",
    "....D.........",
    "..............",
};

static void assertLayout(const char* const* expected, bool shiftHeld) {
    for (uint8_t row = 0; row < LAYOUT_ROWS; row++) {
        TEST_ASSERT_EQUAL_UINT(LAYOUT_COLS, strlen(expected[row]));
        for (uint8_t col = 0; col < LAYOUT_COLS; col++) {
            const char wanted = expected[row][col] == NOTHING_TYPED ? '\0' : expected[row][col];
            char message[64];
            snprintf(message, sizeof message, "row %u col %u shift %d", row, col, shiftHeld ? 1 : 0);
            TEST_ASSERT_EQUAL_CHAR_MESSAGE(wanted, cardputerCharacter(CardputerKey{row, col}, shiftHeld), message);
        }
    }
}

void test_the_whole_layout_types_only_the_grammars_characters() {
    assertLayout(UNSHIFTED_LAYOUT, false);
    assertLayout(SHIFTED_LAYOUT, true);
}

// There is no unshifted '+' on this keyboard, which is the whole reason the driver follows Shift.
void test_plus_needs_shift_and_minus_does_not() {
    TEST_ASSERT_EQUAL_CHAR('+', cardputerCharacter(cardputer_keys::PLUS, true));
    TEST_ASSERT_EQUAL_CHAR('\0', cardputerCharacter(cardputer_keys::PLUS, false));
    TEST_ASSERT_EQUAL_CHAR('-', cardputerCharacter(cardputer_keys::MINUS, false));
}

// The keys the game binds are not characters: the entry reinterprets their actions instead, so a
// typed expression can never contain an Enter.
void test_the_bound_keys_and_shift_type_nothing() {
    const CardputerKey bound[] = {cardputer_keys::ENTER, cardputer_keys::BACKSPACE, cardputer_keys::UP,
                                  cardputer_keys::DOWN, cardputer_keys::SHIFT};
    for (CardputerKey key : bound) {
        TEST_ASSERT_EQUAL_CHAR('\0', cardputerCharacter(key, false));
        TEST_ASSERT_EQUAL_CHAR('\0', cardputerCharacter(key, true));
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_fifo_entry_is_rejected);
    RUN_TEST(test_gpi_event_is_rejected);
    RUN_TEST(test_unwired_matrix_column_is_rejected);
    RUN_TEST(test_unwired_matrix_row_is_rejected);
    RUN_TEST(test_pressed_bit_sets_pressed_and_clear_bit_means_released);
    RUN_TEST(test_every_wired_key_lands_on_a_distinct_printed_key);
    RUN_TEST(test_enter_is_matrix_row_6_col_6);
    RUN_TEST(test_backspace_is_matrix_row_6_col_4);
    RUN_TEST(test_the_whole_layout_types_only_the_grammars_characters);
    RUN_TEST(test_plus_needs_shift_and_minus_does_not);
    RUN_TEST(test_the_bound_keys_and_shift_type_nothing);
    return UNITY_END();
}

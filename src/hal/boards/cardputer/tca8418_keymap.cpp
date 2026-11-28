#include "tca8418_keymap.h"

// Event byte layout (TCA8418 datasheet, "Key Event Table"):
// bit 7 = pressed, bits 0-6 = key number, numbered row * 10 + col + 1.
static constexpr uint8_t EVENT_PRESSED_BIT = 0x80;
static constexpr uint8_t EVENT_KEY_MASK = 0x7F;
static constexpr uint8_t KEY_NUMBERS_PER_ROW = 10;

// The ADV uses 7 controller rows and 8 controller columns.
static constexpr uint8_t MATRIX_ROWS = 7;
static constexpr uint8_t MATRIX_COLS = 8;

// Each controller row feeds two adjacent printed columns: controller columns
// 0-3 are the four printed rows of the left one, 4-7 those of the right one.
static constexpr uint8_t PRINTED_ROWS = 4;
static constexpr uint8_t PRINTED_COLUMNS_PER_CONTROLLER_ROW = MATRIX_COLS / PRINTED_ROWS;

bool decodeTca8418Event(uint8_t eventCode, CardputerKeyEvent& event) {
    uint8_t keyNumber = eventCode & EVENT_KEY_MASK;
    if (keyNumber == 0) return false;

    uint8_t matrixRow = (keyNumber - 1) / KEY_NUMBERS_PER_ROW;
    uint8_t matrixCol = (keyNumber - 1) % KEY_NUMBERS_PER_ROW;
    if (matrixRow >= MATRIX_ROWS || matrixCol >= MATRIX_COLS) return false;

    event.pressed = (eventCode & EVENT_PRESSED_BIT) != 0;
    event.key.row = matrixCol % PRINTED_ROWS;
    event.key.col = matrixRow * PRINTED_COLUMNS_PER_CONTROLLER_ROW + matrixCol / PRINTED_ROWS;
    return true;
}

// The characters dice notation needs, at the printed positions the driver reports for them
// (ADR-0036, slice 0037). The printed layout is 4 rows of 14: row 0 is the number row from the
// backtick, row 2 runs Fn, Shift, a, s, d, and the two shifted entries are the keys whose
// characters are printed above the unshifted one.
//
// The evidence for each position, stated because only four of the 56 printed keys have ever been
// confirmed against hardware and these are not among them:
//   - the keycaps say where a character is: the number row runs ` 1 2 3 4 5 6 7 8 9 0 - =, and
//     'd' is the third letter of the home row, after Fn and Shift;
//   - `decodeTca8418Event` maps all 56 wired keys to distinct printed positions, which
//     `test_tca8418_keymap` holds, so no two of these can be the same key;
//   - the four confirmed positions (Enter and Backspace at column 13, the arrows printed on ';'
//     and '.') are what that mapping gives for them, across three of its four rows.
// That is corroboration and not measurement, which is why the ledger carries confirming each of
// these on a Cardputer ADV: the driver prints `[kbd] down row R col C` for every key it reads, so
// the confirmation is to press them and read the log.
namespace {

constexpr char NO_KEY_CHARACTER = '\0';
constexpr uint8_t NUMBER_ROW = 0;
// The number row starts at the backtick, so '1' is one column in and '0' is the tenth.
constexpr uint8_t FIRST_DIGIT_COLUMN = 1;
constexpr uint8_t DIGIT_COUNT = 10;

struct CharacterKey {
    CardputerKey key;
    char         plain;
    char         shifted;
};

constexpr CharacterKey CHARACTER_KEYS[] = {
    // 'd' is the one letter the grammar uses, and shift spells the D the parser also accepts.
    {cardputer_keys::DIE, 'd', 'D'},
    // '-' is printed unshifted on the key that shifts to '_', which is not notation.
    {cardputer_keys::MINUS, '-', NO_KEY_CHARACTER},
    // '+' is the shifted '=', so an expression that adds needs Shift held. There is no unshifted
    // plus on this keyboard, which is why the driver tracks the key at all.
    {cardputer_keys::PLUS, NO_KEY_CHARACTER, '+'},
};

} // namespace

char cardputerCharacter(CardputerKey key, bool shiftHeld) {
    if (key.row == NUMBER_ROW && key.col >= FIRST_DIGIT_COLUMN && key.col < FIRST_DIGIT_COLUMN + DIGIT_COUNT) {
        // The row reads 1 to 9 and then 0, so the tenth digit key is the zero and not a ten.
        const uint8_t place = key.col - FIRST_DIGIT_COLUMN + 1;
        // Shifted digits are punctuation the grammar has no use for, so they type nothing.
        if (shiftHeld) return NO_KEY_CHARACTER;
        return place == DIGIT_COUNT ? '0' : static_cast<char>('0' + place);
    }
    for (const CharacterKey& character : CHARACTER_KEYS) {
        if (!(key == character.key)) continue;
        return shiftHeld ? character.shifted : character.plain;
    }
    return NO_KEY_CHARACTER;
}

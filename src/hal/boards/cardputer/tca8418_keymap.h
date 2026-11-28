#ifndef TCA8418_KEYMAP_H
#define TCA8418_KEYMAP_H

#include <stdint.h>

/**
 * @brief A key on the Cardputer keyboard, addressed as printed:
 * 4 rows × 14 columns, row 0 = the number row, column 0 = leftmost.
 */
struct CardputerKey {
    uint8_t row;
    uint8_t col;
};

inline bool operator==(const CardputerKey& a, const CardputerKey& b) {
    return a.row == b.row && a.col == b.col;
}

namespace cardputer_keys {
constexpr CardputerKey BACKSPACE{0, 13};
constexpr CardputerKey ENTER{2, 13};
// The arrows are printed on ';' and '.'; this driver reads raw keys, so no Fn is needed.
constexpr CardputerKey UP{2, 11};
constexpr CardputerKey DOWN{3, 11};
// Shift is a key in the same matrix as the rest, so the driver sees it held like any other.
constexpr CardputerKey SHIFT{2, 1};
// The keys dice notation spells besides the digits (ADR-0036). MINUS is printed unshifted; PLUS is
// the shifted '=', because this keyboard has no unshifted plus at all.
constexpr CardputerKey DIE{2, 4};
constexpr CardputerKey MINUS{0, 11};
constexpr CardputerKey PLUS{0, 12};
} // namespace cardputer_keys

/**
 * @brief The character @p key types, with @p shiftHeld, or `'\0'` for a key that types none.
 *
 * Only the characters dice notation accepts are spelled: the digits, `d`, `+` and `-` (ADR-0036).
 * A key outside the grammar types nothing on purpose, because the entry field would otherwise
 * hold an input that is unparseable by construction. The rest of the printed layout waits for a
 * feature that needs it: the keys it would spell are measured the same way these were, by pressing
 * them and reading the row and column the driver reports.
 */
char cardputerCharacter(CardputerKey key, bool shiftHeld);

struct CardputerKeyEvent {
    CardputerKey key;
    bool pressed;
};

/**
 * @brief Decode one TCA8418 FIFO entry into a key on the printed layout.
 *
 * The ADV wires its 56 keys as a 7×8 controller matrix that does not match the
 * printed layout; the remap follows M5Stack's reference driver (M5Cardputer
 * library, TCA8418KeyboardReader::remap).
 *
 * @return false for anything that is not a key in that 7×8 matrix — an empty
 *         FIFO (0), a GPI event, or a matrix position no key is wired to.
 */
bool decodeTca8418Event(uint8_t eventCode, CardputerKeyEvent& event);

#endif // TCA8418_KEYMAP_H

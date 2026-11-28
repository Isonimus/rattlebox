#include <string.h>

#include <unity.h>

#include "../../src/hal/press_gesture.h"
#include "../../src/hal/press_gesture.cpp"
#include "../../src/hal/boards/cardputer/tca8418_keymap.h"
#include "../../src/hal/boards/cardputer/tca8418_keymap.cpp"
#include "../../src/hal/boards/cardputer/keyboard_input.h"
#include "../../src/hal/boards/cardputer/keyboard_input.cpp"

// What the Cardputer's keys mean, with no I2C in it (slice 0037). The driver reads the controller
// and hands the decoded events here, so this is where a keypress's whole decision is checked.

static const uint32_t AT_ONCE = 1000;  // one clock reading: a batch the loop drained in one frame
static const uint32_t LONG_PRESS_MS = 1000;

static void press(KeyboardInput& keyboard, CardputerKey key, uint32_t nowMs = AT_ONCE) {
    keyboard.take(CardputerKeyEvent{key, true}, nowMs);
}

static void release(KeyboardInput& keyboard, CardputerKey key, uint32_t nowMs = AT_ONCE) {
    keyboard.take(CardputerKeyEvent{key, false}, nowMs);
}

/** @brief Tap a key the way a player does: down, then up, both inside one drain. */
static void tap(KeyboardInput& keyboard, CardputerKey key, uint32_t nowMs = AT_ONCE) {
    press(keyboard, key, nowMs);
    release(keyboard, key, nowMs);
}

static CardputerKey digit(char character) {
    // The number row runs 1 to 9 and then 0, one column in from the backtick.
    const uint8_t place = character == '0' ? 10 : static_cast<uint8_t>(character - '0');
    return CardputerKey{0, static_cast<uint8_t>(place)};
}

/**
 * @brief Everything waiting, drained the way the product's loop drains it: every character the
 * queue will give, then one action, until neither has anything left.
 *
 * Characters come out as themselves and actions as a letter, so one string shows the order.
 */
static void drain(KeyboardInput& keyboard, char* out, size_t capacity) {
    size_t at = 0;
    for (;;) {
        for (char typed = keyboard.nextCharacter(); typed != NO_CHARACTER; typed = keyboard.nextCharacter()) {
            if (at + 1 < capacity) out[at++] = typed;
        }
        const InputAction action = keyboard.nextAction();
        if (action == InputAction::NONE) break;
        if (at + 1 < capacity) out[at++] = "nPSudLl"[static_cast<uint8_t>(action)];
    }
    out[at] = '\0';
}

static void assertDrains(KeyboardInput& keyboard, const char* expected) {
    char drained[64];
    drain(keyboard, drained, sizeof drained);
    TEST_ASSERT_EQUAL_STRING(expected, drained);
}

void test_a_typed_character_comes_out_as_itself() {
    KeyboardInput keyboard;
    press(keyboard, digit('3'));
    press(keyboard, cardputer_keys::DIE);
    press(keyboard, digit('6'));
    assertDrains(keyboard, "3d6");
}

void test_a_key_that_types_nothing_queues_nothing() {
    KeyboardInput keyboard;
    // 'a' is the home row's first letter, which no expression can contain.
    press(keyboard, CardputerKey{2, 2});
    press(keyboard, digit('6'));
    assertDrains(keyboard, "6");
}

/**
 * The regression this unit exists for. Characters used to queue while the action latched in a slot
 * of its own, and the loop drained every character before applying the action: a Backspace
 * between two characters deleted the second instead of the first. It takes several events in one
 * drain, which is a frame that ran long, and a screen capture blocks for exactly that long.
 */
void test_a_backspace_between_two_characters_keeps_its_place() {
    KeyboardInput keyboard;
    press(keyboard, digit('3'));
    tap(keyboard, cardputer_keys::BACKSPACE);
    press(keyboard, cardputer_keys::DIE);
    // '3', then SECONDARY, then 'd': the delete falls between them and not after both.
    assertDrains(keyboard, "3Sd");
}

void test_an_enter_in_the_same_batch_submits_what_came_before_it() {
    KeyboardInput keyboard;
    press(keyboard, digit('3'));
    press(keyboard, cardputer_keys::DIE);
    press(keyboard, digit('6'));
    tap(keyboard, cardputer_keys::ENTER);
    press(keyboard, digit('1'));
    // The 1 was typed after Enter, so it belongs to the next expression, not to this one.
    assertDrains(keyboard, "3d6P1");
}

// A second action in one batch used to be dropped, because one slot held only the first. With
// Backspace deleting characters, a dropped one is a keystroke the player made and never saw.
void test_two_taps_in_one_batch_are_both_kept() {
    KeyboardInput keyboard;
    tap(keyboard, cardputer_keys::BACKSPACE);
    tap(keyboard, cardputer_keys::BACKSPACE);
    tap(keyboard, cardputer_keys::ENTER);
    assertDrains(keyboard, "SSP");
}

void test_a_tap_fires_on_release_and_a_hold_fires_while_still_held() {
    KeyboardInput keyboard;
    press(keyboard, cardputer_keys::ENTER, 0);
    // Nothing yet: a tap that fired on the press could also become a long press.
    assertDrains(keyboard, "");
    // PressGesture fires past the threshold and not at it, so a tick at exactly the limit is
    // still a key held and nothing more.
    keyboard.tick(LONG_PRESS_MS);
    assertDrains(keyboard, "");
    keyboard.tick(LONG_PRESS_MS + 1);
    assertDrains(keyboard, "L");
    // And the release after a long press does not also tap.
    release(keyboard, cardputer_keys::ENTER, LONG_PRESS_MS + 2);
    assertDrains(keyboard, "");
}

// There is no unshifted '+' on this keyboard, so an expression that adds needs Shift held.
void test_shift_changes_what_the_next_key_types() {
    KeyboardInput keyboard;
    press(keyboard, cardputer_keys::PLUS);
    assertDrains(keyboard, "");
    press(keyboard, cardputer_keys::SHIFT);
    TEST_ASSERT_TRUE(keyboard.shiftHeld());
    press(keyboard, cardputer_keys::PLUS);
    // A digit shifted is punctuation no expression uses, so it types nothing while Shift is down.
    press(keyboard, digit('2'));
    release(keyboard, cardputer_keys::SHIFT);
    TEST_ASSERT_FALSE(keyboard.shiftHeld());
    press(keyboard, digit('2'));
    assertDrains(keyboard, "+2");
}

void test_shift_is_not_itself_an_input() {
    KeyboardInput keyboard;
    tap(keyboard, cardputer_keys::SHIFT);
    assertDrains(keyboard, "");
}

// A frame's worth never overflows, and what is already queued is never lost to what does not fit:
// the oldest keystrokes are the ones the player can still see on screen.
void test_the_queue_holds_a_whole_drain_and_counts_what_it_cannot() {
    KeyboardInput keyboard;
    for (uint8_t at = 0; at < KEY_EVENTS_PER_DRAIN; at++) press(keyboard, digit('9'));
    TEST_ASSERT_EQUAL_UINT8(0, keyboard.takeDropped());

    char drained[64];
    drain(keyboard, drained, sizeof drained);
    TEST_ASSERT_EQUAL_UINT(KEY_EVENTS_PER_DRAIN, strlen(drained));

    // Past the queue's whole capacity, which no single frame can reach, the newest are refused and
    // counted rather than quietly overwriting the oldest.
    for (uint8_t at = 0; at < KEY_EVENTS_PER_DRAIN + 10; at++) press(keyboard, digit('1'));
    const uint8_t dropped = keyboard.takeDropped();
    TEST_ASSERT_GREATER_THAN_UINT8(0, dropped);
    // Counted once, and the count clears when it is read, so the driver reports each drop once.
    TEST_ASSERT_EQUAL_UINT8(0, keyboard.takeDropped());
    drain(keyboard, drained, sizeof drained);
    TEST_ASSERT_EQUAL_UINT(KEY_EVENTS_PER_DRAIN + 10 - dropped, strlen(drained));
}

void test_an_empty_keyboard_gives_nothing() {
    KeyboardInput keyboard;
    TEST_ASSERT_EQUAL_CHAR(NO_CHARACTER, keyboard.nextCharacter());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(InputAction::NONE), static_cast<uint8_t>(keyboard.nextAction()));
    keyboard.tick(AT_ONCE);
    assertDrains(keyboard, "");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_typed_character_comes_out_as_itself);
    RUN_TEST(test_a_key_that_types_nothing_queues_nothing);
    RUN_TEST(test_a_backspace_between_two_characters_keeps_its_place);
    RUN_TEST(test_an_enter_in_the_same_batch_submits_what_came_before_it);
    RUN_TEST(test_two_taps_in_one_batch_are_both_kept);
    RUN_TEST(test_a_tap_fires_on_release_and_a_hold_fires_while_still_held);
    RUN_TEST(test_shift_changes_what_the_next_key_types);
    RUN_TEST(test_shift_is_not_itself_an_input);
    RUN_TEST(test_the_queue_holds_a_whole_drain_and_counts_what_it_cannot);
    RUN_TEST(test_an_empty_keyboard_gives_nothing);
    return UNITY_END();
}

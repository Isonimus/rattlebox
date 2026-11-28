#include <unity.h>

#include "../../src/geometry/die_numbering.h"
#include "../../src/geometry/die_numbering.cpp"
#include "../../src/geometry/die_shape.h"
#include "../../src/geometry/die_shape.cpp"
#include "../../src/roll/die_catalog.h"
#include "../../src/roll/die_catalog.cpp"
#include "../../src/roll/dice_notation.h"
#include "../../src/roll/dice_notation.cpp"
#include "../../src/app/serial_commands.h"
#include "../../src/app/serial_commands.cpp"

// The host's one-byte commands to the product (slice 0011), beside the screen capture's 'S'. Since
// slice 0037 a character of dice notation is one too, so a script can type an expression where a
// player would (ADR-0036).

static void assertCommand(SerialCommand expected, int byte) {
    const SerialRequest request = serialCommandFor(byte);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(request.command));
    if (expected != SerialCommand::Type) TEST_ASSERT_EQUAL_CHAR(NO_CHARACTER, request.character);
}

static void assertTyped(char character) {
    const SerialRequest request = serialCommandFor(character);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SerialCommand::Type), static_cast<uint8_t>(request.command));
    TEST_ASSERT_EQUAL_CHAR(character, request.character);
}

void test_each_command_byte() {
    assertCommand(SerialCommand::Roll, 'R');
    assertCommand(SerialCommand::PressPrimary, 'T');
    assertCommand(SerialCommand::PressSecondary, 'C');
}

// Every character the grammar accepts arrives as itself, so the host types what the player types.
void test_every_notation_character_is_typed() {
    for (const char* character = "0123456789dD+-"; *character != '\0'; character++) assertTyped(*character);
}

void test_other_bytes_are_no_command() {
    // 'S' is the screen capture's, and a letter that is neither a command nor notation is not a
    // roll: a terminal's stray typing changes nothing.
    const int others[] = {'S', 'r', 't', 'c', 'k', ' ', '=', '\n', 0, 0xFF, -1};
    for (int byte : others) assertCommand(SerialCommand::None, byte);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_each_command_byte);
    RUN_TEST(test_every_notation_character_is_typed);
    RUN_TEST(test_other_bytes_are_no_command);
    return UNITY_END();
}

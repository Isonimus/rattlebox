#include <unity.h>
#include "../../src/app/serial_commands.h"
#include "../../src/app/serial_commands.cpp"

// The host's one-byte commands to the product (slice 0011), beside the screen capture's 'S'.

static void assertCommand(SerialCommand expected, int byte) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(serialCommandFor(byte)));
}

void test_each_command_byte() {
    assertCommand(SerialCommand::Roll, 'R');
    assertCommand(SerialCommand::PressPrimary, 'T');
    assertCommand(SerialCommand::PressSecondary, 'C');
}

void test_other_bytes_are_no_command() {
    // 'S' is the screen capture's, and lower case is not accepted: a terminal's stray typing is
    // not a roll.
    const int others[] = {'S', 'r', 't', 'c', '\n', 0, 0xFF};
    for (int byte : others) assertCommand(SerialCommand::None, byte);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_each_command_byte);
    RUN_TEST(test_other_bytes_are_no_command);
    return UNITY_END();
}

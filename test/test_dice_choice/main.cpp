#include <unity.h>
#include "../../src/roll/dice_choice.h"
#include "../../src/roll/dice_choice.cpp"

// The product's controls (slice 0011): PRIMARY steps the type in ascending order (slice 0026),
// SECONDARY cycles the count, and the Cardputer's UP and DOWN step it.

static void assertChange(DiceChoice expected, bool changed, ChoiceChange actual) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected.type), static_cast<uint8_t>(actual.choice.type));
    TEST_ASSERT_EQUAL_UINT8(expected.count, actual.choice.count);
    TEST_ASSERT_EQUAL(changed, actual.changed);
}

void test_boot_is_one_d20() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DieType::D20), static_cast<uint8_t>(BOOT_CHOICE.type));
    TEST_ASSERT_EQUAL_UINT8(1, BOOT_CHOICE.count);
}

void test_primary_steps_the_type_up_wraps_round_and_keeps_the_count() {
    assertChange({DieType::D6, 2}, true, applyAction({DieType::D4, 2}, InputAction::PRIMARY));
    assertChange({DieType::D8, 2}, true, applyAction({DieType::D6, 2}, InputAction::PRIMARY));
    assertChange({DieType::D10, 2}, true, applyAction({DieType::D8, 2}, InputAction::PRIMARY));
    assertChange({DieType::D12, 2}, true, applyAction({DieType::D10, 2}, InputAction::PRIMARY));
    assertChange({DieType::D20, 2}, true, applyAction({DieType::D12, 2}, InputAction::PRIMARY));
    assertChange({DieType::D4, 2}, true, applyAction({DieType::D20, 2}, InputAction::PRIMARY));
}

void test_secondary_cycles_the_count_through_every_value() {
    assertChange({DieType::D6, 2}, true, applyAction({DieType::D6, 1}, InputAction::SECONDARY));
    assertChange({DieType::D6, 3}, true, applyAction({DieType::D6, 2}, InputAction::SECONDARY));
    assertChange({DieType::D6, 1}, true, applyAction({DieType::D6, 3}, InputAction::SECONDARY));
}

void test_up_and_down_step_the_count_and_stop_at_the_ends() {
    assertChange({DieType::D20, 2}, true, applyAction({DieType::D20, 1}, InputAction::UP));
    assertChange({DieType::D20, 3}, true, applyAction({DieType::D20, 2}, InputAction::UP));
    assertChange({DieType::D20, 3}, false, applyAction({DieType::D20, 3}, InputAction::UP));
    assertChange({DieType::D20, 2}, true, applyAction({DieType::D20, 3}, InputAction::DOWN));
    assertChange({DieType::D20, 1}, true, applyAction({DieType::D20, 2}, InputAction::DOWN));
    assertChange({DieType::D20, 1}, false, applyAction({DieType::D20, 1}, InputAction::DOWN));
}

void test_other_actions_change_nothing() {
    const InputAction others[] = {InputAction::NONE, InputAction::LONG_PRIMARY, InputAction::LONG_SECONDARY};
    for (InputAction action : others) assertChange({DieType::D6, 2}, false, applyAction({DieType::D6, 2}, action));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_boot_is_one_d20);
    RUN_TEST(test_primary_steps_the_type_up_wraps_round_and_keeps_the_count);
    RUN_TEST(test_secondary_cycles_the_count_through_every_value);
    RUN_TEST(test_up_and_down_step_the_count_and_stop_at_the_ends);
    RUN_TEST(test_other_actions_change_nothing);
    return UNITY_END();
}

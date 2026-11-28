#include <unity.h>
#include "../../src/geometry/die_numbering.h"
#include "../../src/geometry/die_numbering.cpp"
#include "../../src/geometry/die_shape.h"
#include "../../src/geometry/die_shape.cpp"
#include "../../src/roll/die_catalog.h"
#include "../../src/roll/die_catalog.cpp"
#include "../../src/roll/dice_choice.h"
#include "../../src/roll/dice_choice.cpp"

// The product's controls (slice 0011): PRIMARY steps the type in ascending order (slice 0026),
// SECONDARY cycles the count, and the Cardputer's UP and DOWN step it.

static void assertChange(DiceChoice expected, bool changed, ChoiceChange actual) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected.type), static_cast<uint8_t>(actual.choice.type));
    TEST_ASSERT_EQUAL_UINT8(expected.count, actual.choice.count);
    TEST_ASSERT_EQUAL_INT8(expected.modifier, actual.choice.modifier);
    TEST_ASSERT_EQUAL(changed, actual.changed);
}

void test_boot_is_one_d20() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DieType::D20), static_cast<uint8_t>(BOOT_CHOICE.type));
    TEST_ASSERT_EQUAL_UINT8(1, BOOT_CHOICE.count);
    TEST_ASSERT_EQUAL_INT8(0, BOOT_CHOICE.modifier);
}

// Slice 0037. No action can express a modifier, and the band shows the one a typed expression set
// (ADR-0036), so stepping the die or the count keeps it rather than dropping it in silence.
void test_every_action_carries_the_modifier_through() {
    const InputAction actions[] = {InputAction::PRIMARY,       InputAction::SECONDARY,
                                   InputAction::UP,            InputAction::DOWN,
                                   InputAction::LONG_PRIMARY,  InputAction::LONG_SECONDARY,
                                   InputAction::NONE};
    for (InputAction action : actions) {
        const ChoiceChange change = applyAction({DieType::D6, 2, -3}, action);
        TEST_ASSERT_EQUAL_INT8(-3, change.choice.modifier);
    }
    // Clamping the count onto a narrower type is the one step that rewrites the choice, and it
    // leaves the modifier alone too.
    assertChange({DieType::D100, 1, 4}, true, applyAction({DieType::D20, 3, 4}, InputAction::PRIMARY));
}

void test_primary_steps_the_type_up_wraps_round_and_keeps_the_count() {
    assertChange({DieType::D6, 2}, true, applyAction({DieType::D4, 2}, InputAction::PRIMARY));
    assertChange({DieType::D8, 2}, true, applyAction({DieType::D6, 2}, InputAction::PRIMARY));
    assertChange({DieType::D10, 2}, true, applyAction({DieType::D8, 2}, InputAction::PRIMARY));
    assertChange({DieType::D12, 2}, true, applyAction({DieType::D10, 2}, InputAction::PRIMARY));
    assertChange({DieType::D20, 2}, true, applyAction({DieType::D12, 2}, InputAction::PRIMARY));
    // The d% is last, and it holds one die, so the count it is stepped onto with comes down to 1.
    assertChange({DieType::D100, 1}, true, applyAction({DieType::D20, 2}, InputAction::PRIMARY));
    assertChange({DieType::D4, 1}, true, applyAction({DieType::D100, 1}, InputAction::PRIMARY));
}

// Slice 0035. A d% is two bodies of the three the tray holds, so a second pair has no room. The
// count control is not dead on it: it is the clamp UP and DOWN already had, reaching its one value.
void test_the_count_controls_hold_a_percentile_roll_at_one_die() {
    const InputAction counting[] = {InputAction::SECONDARY, InputAction::UP, InputAction::DOWN};
    for (InputAction action : counting) {
        assertChange({DieType::D100, 1}, false, applyAction({DieType::D100, 1}, action));
    }
}

// Stepping onto a type that holds fewer dice brings the count down, from any count.
void test_stepping_onto_the_percentile_die_brings_any_count_down_to_one() {
    for (uint8_t count = 1; count <= MAX_DICE; count++) {
        assertChange({DieType::D100, 1}, true, applyAction({DieType::D20, count}, InputAction::PRIMARY));
    }
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
    RUN_TEST(test_every_action_carries_the_modifier_through);
    RUN_TEST(test_primary_steps_the_type_up_wraps_round_and_keeps_the_count);
    RUN_TEST(test_the_count_controls_hold_a_percentile_roll_at_one_die);
    RUN_TEST(test_stepping_onto_the_percentile_die_brings_any_count_down_to_one);
    RUN_TEST(test_secondary_cycles_the_count_through_every_value);
    RUN_TEST(test_up_and_down_step_the_count_and_stop_at_the_ends);
    RUN_TEST(test_other_actions_change_nothing);
    return UNITY_END();
}

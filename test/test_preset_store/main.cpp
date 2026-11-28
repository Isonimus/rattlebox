#include <string.h>

#include <unity.h>

#include "../../src/geometry/die_numbering.h"
#include "../../src/geometry/die_numbering.cpp"
#include "../../src/geometry/die_shape.h"
#include "../../src/geometry/die_shape.cpp"
#include "../../src/render/results_band.h"
#include "../../src/render/results_band.cpp"
#include "../../src/roll/die_catalog.h"
#include "../../src/roll/die_catalog.cpp"
#include "../../src/roll/preset_store.h"
#include "../../src/roll/preset_store.cpp"

// What the device remembers across a power cycle (slice 0039): the roll it last threw and four
// preset slots. The store is pure, so the record's whole decision is checked here, with no flash
// in it. The one claim this cannot make is that a write survives a reset, which is what
// scripts/presets-verify.mjs is for.

static void assertChoice(DiceChoice expected, DiceChoice actual) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected.type), static_cast<uint8_t>(actual.type));
    TEST_ASSERT_EQUAL_UINT8(expected.count, actual.count);
    TEST_ASSERT_EQUAL_INT8(expected.modifier, actual.modifier);
}

/** @brief A record serialised and read back, as a power cycle does it. */
static bool roundTrip(const PresetRecord& record, PresetRecord& into) {
    uint8_t bytes[PRESET_RECORD_BYTES];
    const size_t written = writePresets(record, bytes, sizeof bytes);
    TEST_ASSERT_EQUAL_UINT(PRESET_RECORD_BYTES, written);
    return readPresets(bytes, written, into);
}

/** @brief A valid record's bytes, for a test that then corrupts one field of them. */
static void validBytes(uint8_t* bytes) {
    const PresetSave saved = savePreset(emptyPresets(), DiceChoice{DieType::D6, 3, 2});
    TEST_ASSERT_EQUAL_UINT(PRESET_RECORD_BYTES, writePresets(saved.record, bytes, PRESET_RECORD_BYTES));
}

/** @brief Read bytes that were valid until @p at was set to @p value, and expect them refused. */
static void assertRefusedWith(size_t at, uint8_t value) {
    uint8_t bytes[PRESET_RECORD_BYTES];
    validBytes(bytes);
    // The bytes are valid until this one is changed, so a refusal can only be this field's doing.
    PresetRecord check{};
    TEST_ASSERT_TRUE(readPresets(bytes, sizeof bytes, check));
    bytes[at] = value;
    PresetRecord into = emptyPresets();
    TEST_ASSERT_FALSE(readPresets(bytes, sizeof bytes, into));
}

void test_an_empty_store_boots_to_the_boot_choice() {
    const PresetRecord record = emptyPresets();
    assertChoice(BOOT_CHOICE, record.last);
    TEST_ASSERT_EQUAL_UINT8(0, record.savedSlots);
}

void test_a_record_survives_a_round_trip() {
    PresetRecord record = emptyPresets();
    record.last = DiceChoice{DieType::D100, 1, -9};
    record = savePreset(record, DiceChoice{DieType::D6, 3, 2}).record;
    record = savePreset(record, DiceChoice{DieType::D4, 2, 0}).record;

    PresetRecord read{};
    TEST_ASSERT_TRUE(roundTrip(record, read));
    assertChoice(record.last, read.last);
    TEST_ASSERT_EQUAL_UINT8(2, read.savedSlots);
    assertChoice(DiceChoice{DieType::D6, 3, 2}, read.slots[0]);
    assertChoice(DiceChoice{DieType::D4, 2, 0}, read.slots[1]);
}

// The record is read back out of a partition the player can format from the Launcher and that
// another firmware may have written, so nothing in it is trusted (CLAUDE.md §4, invariant 9).
void test_a_record_of_the_wrong_length_is_refused() {
    uint8_t bytes[PRESET_RECORD_BYTES];
    validBytes(bytes);
    PresetRecord into = emptyPresets();
    TEST_ASSERT_FALSE(readPresets(bytes, PRESET_RECORD_BYTES - 1, into));
    TEST_ASSERT_FALSE(readPresets(bytes, 0, into));
}

void test_another_versions_record_is_refused() {
    assertRefusedWith(0, PRESET_RECORD_VERSION + 1);
}

void test_a_count_above_the_types_most_is_refused() {
    // Byte 4 is the remembered choice's type and byte 5 its count, and the remembered choice of a
    // record that has only saved a slot is still BOOT_CHOICE's D20, which holds three dice. Four
    // is a count the tray cannot throw and the rest of the firmware does not expect, and zero is
    // a roll of no dice at all.
    assertRefusedWith(5, MAX_DICE + 1);
    assertRefusedWith(5, 0);
}

void test_a_modifier_past_the_limit_is_refused() {
    assertRefusedWith(6, static_cast<uint8_t>(MODIFIER_LIMIT + 1));
    assertRefusedWith(6, static_cast<uint8_t>(-MODIFIER_LIMIT - 1));
}

// dieKind() aborts on a type outside the enum, so a record holding one must be refused before any
// catalog call is made with it. Without the raw-byte check this test takes the process down.
void test_a_type_outside_the_catalog_is_refused() {
    assertRefusedWith(4, DIE_TYPE_COUNT);
    assertRefusedWith(4, 0xFF);
}

void test_a_cursor_outside_its_bound_is_refused() {
    // The saved count, the save cursor and the recall cursor, in that order.
    assertRefusedWith(1, PRESET_SLOTS + 1);
    assertRefusedWith(2, PRESET_SLOTS);
    // One slot is saved in the valid record, so slot 1 holds nothing to recall.
    assertRefusedWith(3, 1);
}

/**
 * A record whose cursors are each inside their own bound but disagree with each other.
 *
 * Accepting it let the next save skip a slot, so a slot below savedSlots held no choice and a
 * later recall returned an all-zero one: a roll of no dice, written to flash on the way past.
 */
void test_cursors_that_disagree_with_each_other_are_refused() {
    uint8_t bytes[PRESET_RECORD_BYTES];
    validBytes(bytes);
    PresetRecord check{};
    TEST_ASSERT_TRUE(readPresets(bytes, sizeof bytes, check));
    TEST_ASSERT_EQUAL_UINT8(1, check.savedSlots);

    // One slot is saved, so the next save belongs in slot 1. Slot 3 is inside the bound and wrong.
    assertRefusedWith(2, 3);
    // And the firmware's own pairing is still accepted, so the rule refuses only what it should.
    PresetRecord valid{};
    bytes[2] = 1;
    TEST_ASSERT_TRUE(readPresets(bytes, sizeof bytes, valid));
    // Once every slot is occupied the cursor has wrapped, so any slot is a legitimate next save.
    PresetRecord full = emptyPresets();
    for (uint8_t at = 0; at < PRESET_SLOTS; at++) {
        full = savePreset(full, DiceChoice{DieType::D6, 1, 0}).record;
    }
    TEST_ASSERT_EQUAL_UINT8(PRESET_SLOTS, full.savedSlots);
    TEST_ASSERT_EQUAL_UINT8(0, full.nextSave);
    PresetRecord wrapped{};
    TEST_ASSERT_TRUE(roundTrip(full, wrapped));
}

void test_a_slot_is_refused_for_its_own_bad_fields() {
    // Byte 7 is the first slot's type: an occupied slot is validated like the remembered choice.
    assertRefusedWith(7, DIE_TYPE_COUNT);
}

void test_an_invalid_record_leaves_the_caller_untouched() {
    uint8_t bytes[PRESET_RECORD_BYTES];
    validBytes(bytes);
    bytes[0] = PRESET_RECORD_VERSION + 1;

    PresetRecord into = emptyPresets();
    into.last = DiceChoice{DieType::D12, 2, 3};
    TEST_ASSERT_FALSE(readPresets(bytes, sizeof bytes, into));
    // The caller's record is what it falls back to, so a refused read must not half-write it.
    assertChoice(DiceChoice{DieType::D12, 2, 3}, into.last);
}

void test_saves_fill_the_slots_in_order() {
    PresetRecord record = emptyPresets();
    for (uint8_t slot = 0; slot < PRESET_SLOTS; slot++) {
        const PresetSave saved = savePreset(record, DiceChoice{DieType::D6, 1, static_cast<int8_t>(slot)});
        TEST_ASSERT_EQUAL_UINT8(slot, saved.slot);
        record = saved.record;
        TEST_ASSERT_EQUAL_UINT8(slot + 1, record.savedSlots);
    }
    // A fifth save overwrites the oldest rather than being refused: the player asked to keep this
    // roll, and four is the number the control can reach, not a quota to report on.
    const PresetSave fifth = savePreset(record, DiceChoice{DieType::D20, 1, 0});
    TEST_ASSERT_EQUAL_UINT8(0, fifth.slot);
    TEST_ASSERT_EQUAL_UINT8(PRESET_SLOTS, fifth.record.savedSlots);
    assertChoice(DiceChoice{DieType::D20, 1, 0}, fifth.record.slots[0]);
}

void test_a_recall_returns_the_slot_just_saved_then_moves_on() {
    PresetRecord record = emptyPresets();
    record = savePreset(record, DiceChoice{DieType::D6, 3, 2}).record;
    record = savePreset(record, DiceChoice{DieType::D4, 2, 0}).record;

    // The slot just saved comes back first: it is what the player last chose to keep.
    const PresetRecall first = recallPreset(record, BOOT_CHOICE);
    TEST_ASSERT_TRUE(first.found);
    TEST_ASSERT_EQUAL_UINT8(1, first.slot);
    assertChoice(DiceChoice{DieType::D4, 2, 0}, first.choice);

    const PresetRecall second = recallPreset(first.record, first.choice);
    TEST_ASSERT_TRUE(second.found);
    TEST_ASSERT_EQUAL_UINT8(0, second.slot);
    assertChoice(DiceChoice{DieType::D6, 3, 2}, second.choice);

    // And it comes back around rather than stopping at the end.
    const PresetRecall third = recallPreset(second.record, second.choice);
    TEST_ASSERT_TRUE(third.found);
    TEST_ASSERT_EQUAL_UINT8(1, third.slot);
}

// A hold on a device that has saved nothing must not roll something arbitrary.
void test_a_recall_with_nothing_saved_changes_nothing() {
    const DiceChoice current{DieType::D12, 2, 3};
    const PresetRecall recall = recallPreset(emptyPresets(), current);
    TEST_ASSERT_FALSE(recall.found);
    assertChoice(current, recall.choice);
    TEST_ASSERT_EQUAL_UINT8(0, recall.record.savedSlots);
}

void test_the_recall_cursor_survives_a_round_trip() {
    PresetRecord record = emptyPresets();
    record = savePreset(record, DiceChoice{DieType::D6, 3, 2}).record;
    record = savePreset(record, DiceChoice{DieType::D4, 2, 0}).record;
    // Slot 1 was saved last, so that is where the cursor stands: a value that is deliberately not
    // emptyPresets()'s zero, because a cursor that was never serialised would read back as zero
    // and an assertion expecting zero would pass either way.
    TEST_ASSERT_EQUAL_UINT8(1, record.recallAt);

    PresetRecord read{};
    TEST_ASSERT_TRUE(roundTrip(record, read));
    // Where the cycle had got to is part of the record, so a power cycle does not restart it.
    const PresetRecall after = recallPreset(read, BOOT_CHOICE);
    TEST_ASSERT_EQUAL_UINT8(1, after.slot);
    assertChoice(DiceChoice{DieType::D4, 2, 0}, after.choice);
}

void test_a_small_buffer_writes_nothing() {
    PresetRecord record = emptyPresets();
    uint8_t bytes[PRESET_RECORD_BYTES];
    memset(bytes, 0xAA, sizeof bytes);
    TEST_ASSERT_EQUAL_UINT(0, writePresets(record, bytes, PRESET_RECORD_BYTES - 1));
    // Nothing was written, so the caller's buffer is untouched rather than half a record.
    TEST_ASSERT_EQUAL_UINT8(0xAA, bytes[0]);
}

// Every message the holds can put in the band goes through messageBand, which aborts on text it
// cannot draw: the static_asserts bound the lengths, and this proves the formatting agrees.
void test_every_preset_message_fits_the_band() {
    for (uint8_t slot = 0; slot < PRESET_SLOTS; slot++) {
        TEST_ASSERT_EQUAL_UINT8(1, savedBand(slot).segmentCount);
        TEST_ASSERT_EQUAL_UINT8(1, recalledBand(slot).segmentCount);
    }
    TEST_ASSERT_EQUAL_STRING("saved as 1", savedBand(0).segments[0].text);
    TEST_ASSERT_EQUAL_STRING("preset 4", recalledBand(PRESET_SLOTS - 1).segments[0].text);
    TEST_ASSERT_EQUAL_STRING(PRESET_NONE_SAVED, noPresetsBand().segments[0].text);
}

void test_every_catalog_type_round_trips_at_its_most_dice() {
    for (uint8_t at = 0; at < DIE_TYPE_COUNT; at++) {
        const DieType type = static_cast<DieType>(at);
        PresetRecord record = emptyPresets();
        record.last = DiceChoice{type, maxDiceOf(type), MODIFIER_LIMIT};
        PresetRecord read{};
        TEST_ASSERT_TRUE(roundTrip(record, read));
        assertChoice(record.last, read.last);
        // And one die past what the type holds is refused, for every type and not just the D6.
        PresetRecord tooMany = emptyPresets();
        tooMany.last = DiceChoice{type, static_cast<uint8_t>(maxDiceOf(type) + 1), 0};
        PresetRecord refused{};
        TEST_ASSERT_FALSE(roundTrip(tooMany, refused));
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_an_empty_store_boots_to_the_boot_choice);
    RUN_TEST(test_a_record_survives_a_round_trip);
    RUN_TEST(test_a_record_of_the_wrong_length_is_refused);
    RUN_TEST(test_another_versions_record_is_refused);
    RUN_TEST(test_a_count_above_the_types_most_is_refused);
    RUN_TEST(test_a_modifier_past_the_limit_is_refused);
    RUN_TEST(test_a_type_outside_the_catalog_is_refused);
    RUN_TEST(test_a_cursor_outside_its_bound_is_refused);
    RUN_TEST(test_cursors_that_disagree_with_each_other_are_refused);
    RUN_TEST(test_a_slot_is_refused_for_its_own_bad_fields);
    RUN_TEST(test_an_invalid_record_leaves_the_caller_untouched);
    RUN_TEST(test_saves_fill_the_slots_in_order);
    RUN_TEST(test_a_recall_returns_the_slot_just_saved_then_moves_on);
    RUN_TEST(test_a_recall_with_nothing_saved_changes_nothing);
    RUN_TEST(test_the_recall_cursor_survives_a_round_trip);
    RUN_TEST(test_a_small_buffer_writes_nothing);
    RUN_TEST(test_every_preset_message_fits_the_band);
    RUN_TEST(test_every_catalog_type_round_trips_at_its_most_dice);
    return UNITY_END();
}

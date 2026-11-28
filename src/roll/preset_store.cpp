#include "preset_store.h"

#include <stdio.h>


namespace {

// Where each field sits in the record. Spelled out rather than taken from a struct's layout,
// because padding is the compiler's business and a persisted format is not.
constexpr size_t VERSION_AT = 0;
constexpr size_t SAVED_SLOTS_AT = 1;
constexpr size_t NEXT_SAVE_AT = 2;
constexpr size_t RECALL_AT = 3;
constexpr size_t LAST_CHOICE_AT = PRESET_HEADER_BYTES;
constexpr size_t SLOTS_AT = LAST_CHOICE_AT + CHOICE_BYTES;

// Within one choice.
constexpr size_t TYPE_AT = 0;
constexpr size_t COUNT_AT = 1;
constexpr size_t MODIFIER_AT = 2;

static_assert(CHOICE_BYTES == 3, "a choice is a type, a count and a modifier");

void writeChoice(const DiceChoice& choice, uint8_t* at) {
    at[TYPE_AT] = static_cast<uint8_t>(choice.type);
    at[COUNT_AT] = choice.count;
    at[MODIFIER_AT] = static_cast<uint8_t>(choice.modifier);
}

DiceChoice readChoice(const uint8_t* at) {
    DiceChoice choice{};
    choice.type = static_cast<DieType>(at[TYPE_AT]);
    choice.count = at[COUNT_AT];
    choice.modifier = static_cast<int8_t>(at[MODIFIER_AT]);
    return choice;
}

}  // namespace

PresetRecord emptyPresets() {
    PresetRecord record{};
    record.last = BOOT_CHOICE;
    record.savedSlots = 0;
    record.nextSave = 0;
    record.recallAt = 0;
    return record;
}

bool choiceIsValid(const DiceChoice& choice) {
    // The type is checked as a number first: dieKind() aborts on a value outside the enum, so
    // maxDiceOf() below must not be reached with one (CLAUDE.md §4, invariant 9).
    if (static_cast<uint8_t>(choice.type) >= DIE_TYPE_COUNT) return false;
    if (choice.count < 1 || choice.count > maxDiceOf(choice.type)) return false;
    if (choice.modifier < -MODIFIER_LIMIT || choice.modifier > MODIFIER_LIMIT) return false;
    return true;
}

size_t writePresets(const PresetRecord& record, uint8_t* bytes, size_t capacity) {
    if (capacity < PRESET_RECORD_BYTES) return 0;

    bytes[VERSION_AT] = PRESET_RECORD_VERSION;
    bytes[SAVED_SLOTS_AT] = record.savedSlots;
    bytes[NEXT_SAVE_AT] = record.nextSave;
    bytes[RECALL_AT] = record.recallAt;
    writeChoice(record.last, &bytes[LAST_CHOICE_AT]);
    for (uint8_t slot = 0; slot < PRESET_SLOTS; slot++) {
        writeChoice(record.slots[slot], &bytes[SLOTS_AT + slot * CHOICE_BYTES]);
    }
    return PRESET_RECORD_BYTES;
}

bool readPresets(const uint8_t* bytes, size_t size, PresetRecord& into) {
    if (size != PRESET_RECORD_BYTES) return false;
    if (bytes[VERSION_AT] != PRESET_RECORD_VERSION) return false;

    PresetRecord record = emptyPresets();
    record.savedSlots = bytes[SAVED_SLOTS_AT];
    record.nextSave = bytes[NEXT_SAVE_AT];
    record.recallAt = bytes[RECALL_AT];

    if (record.savedSlots > PRESET_SLOTS) return false;
    if (record.nextSave >= PRESET_SLOTS) return false;
    // The cursors have to agree with each other, not just sit inside their own bounds. While the
    // slots are filling, this firmware always writes the next save at the first unoccupied slot,
    // so a record that says otherwise is one it did not write. Accepting it would let the next
    // save skip a slot, leaving an unoccupied one below savedSlots for a later recall to return:
    // an all-zero choice of no dice at all. Once every slot is occupied the cursor has wrapped and
    // may point anywhere.
    if (record.savedSlots < PRESET_SLOTS && record.nextSave != record.savedSlots) return false;
    // With nothing saved there is no slot to recall, and the cursor must say so rather than point
    // at a slot that holds no choice.
    if (record.savedSlots == 0 ? record.recallAt != 0 : record.recallAt >= record.savedSlots) {
        return false;
    }

    record.last = readChoice(&bytes[LAST_CHOICE_AT]);
    if (!choiceIsValid(record.last)) return false;

    for (uint8_t slot = 0; slot < record.savedSlots; slot++) {
        record.slots[slot] = readChoice(&bytes[SLOTS_AT + slot * CHOICE_BYTES]);
        if (!choiceIsValid(record.slots[slot])) return false;
    }
    // Slots past savedSlots are never read, so whatever they hold is left as emptyPresets() set
    // them rather than carried in: an unoccupied slot has no choice in it to be valid or not.

    into = record;
    return true;
}

PresetSave savePreset(const PresetRecord& record, const DiceChoice& choice) {
    PresetRecord saved = record;
    const uint8_t slot = saved.nextSave;

    saved.slots[slot] = choice;
    saved.nextSave = static_cast<uint8_t>((slot + 1) % PRESET_SLOTS);
    if (saved.savedSlots < PRESET_SLOTS) saved.savedSlots++;
    // The slot just written is the one a recall should reach first: it is what the player last
    // chose to keep, so a save followed by a hold returns it rather than the one after it.
    saved.recallAt = slot;

    return PresetSave{saved, slot};
}

PresetRecall recallPreset(const PresetRecord& record, const DiceChoice& current) {
    if (record.savedSlots == 0) return PresetRecall{record, current, false, 0};

    PresetRecord recalled = record;
    const uint8_t slot = recalled.recallAt;
    recalled.recallAt = static_cast<uint8_t>((slot + 1) % recalled.savedSlots);

    return PresetRecall{recalled, recalled.slots[slot], true, slot};
}

namespace {

// A slot's number is one digit, which is what lets the asserts below bound the messages.
static_assert(PRESET_SLOTS < 10, "a slot's number is one digit in the band's messages");
constexpr size_t SLOT_NUMBER_CHARS = 1;
// Each message is asserted to fit the band, as the notation's reasons are: messageBand aborts on
// text it cannot draw, so a message too long would take the product down rather than be cut short.
static_assert(sizeof(PRESET_SAVED_PREFIX) - 1 + SLOT_NUMBER_CHARS <= BAND_MESSAGE_MAX_CHARS,
              "the save message must fit the band");
static_assert(sizeof(PRESET_RECALLED_PREFIX) - 1 + SLOT_NUMBER_CHARS <= BAND_MESSAGE_MAX_CHARS,
              "the recall message must fit the band");
static_assert(sizeof(PRESET_NONE_SAVED) - 1 <= BAND_MESSAGE_MAX_CHARS,
              "the empty-store message must fit the band");

ResultsBand slotBand(const char* prefix, uint8_t slot) {
    char text[BAND_SEGMENT_CAPACITY];
    // Counted from 1 for the player: this is which of four presets it is, not an array index.
    snprintf(text, sizeof text, "%s%u", prefix, static_cast<unsigned>(slot + 1));
    return messageBand(text);
}

}  // namespace

ResultsBand savedBand(uint8_t slot) { return slotBand(PRESET_SAVED_PREFIX, slot); }

ResultsBand recalledBand(uint8_t slot) { return slotBand(PRESET_RECALLED_PREFIX, slot); }

ResultsBand noPresetsBand() { return messageBand(PRESET_NONE_SAVED); }

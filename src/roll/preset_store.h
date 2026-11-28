#ifndef PRESET_STORE_H
#define PRESET_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "../render/results_band.h"
#include "dice_choice.h"

/**
 * @brief How many choices the device remembers under the holds (ADR-0038).
 *
 * Four is chosen against the control and not against the storage: a choice is three bytes in a
 * 20 KB partition, so what bounds the count is how many holds it takes to come back around with
 * nothing on screen listing the slots.
 */
constexpr uint8_t PRESET_SLOTS = 4;

/**
 * @brief The layout this firmware writes. A record carrying any other number is another version's
 * and is discarded whole rather than guessed at.
 */
constexpr uint8_t PRESET_RECORD_VERSION = 1;

/** @brief One choice on the wire: the type, the count and the modifier, one byte each. */
constexpr size_t CHOICE_BYTES = 3;
/** @brief The version, the saved count, the save cursor and the recall cursor. */
constexpr size_t PRESET_HEADER_BYTES = 4;
/** @brief The whole record: the header, the remembered choice, and the slots. */
constexpr size_t PRESET_RECORD_BYTES = PRESET_HEADER_BYTES + CHOICE_BYTES * (1 + PRESET_SLOTS);

/**
 * @brief Everything kept across a power cycle.
 *
 * The slots fill in order and wrap, so the occupied ones are always 0 to `savedSlots - 1`, and a
 * fifth save overwrites the oldest. The record is serialised field by field rather than written as
 * these bytes, because a struct's padding is the compiler's business and a persisted format is not
 * (slice 0039).
 */
struct PresetRecord {
    /** @brief What the device rolls at boot, in place of `BOOT_CHOICE` once anything is stored. */
    DiceChoice last;
    DiceChoice slots[PRESET_SLOTS];
    /** @brief How many slots hold a choice, 0 to `PRESET_SLOTS`. */
    uint8_t    savedSlots;
    /** @brief The slot the next save writes. */
    uint8_t    nextSave;
    /** @brief The slot the next recall returns, always under `savedSlots` while any are saved. */
    uint8_t    recallAt;
};

/** @brief A store with nothing in it: no slots, and `BOOT_CHOICE` as the roll to come back to. */
PresetRecord emptyPresets();

/**
 * @brief Whether @p choice is one the rest of the firmware can act on.
 *
 * The type is checked as a raw byte against the catalog's count before any catalog call, because
 * `dieKind` aborts on a value outside the enum and a record out of flash can hold any byte at all
 * (CLAUDE.md §4, invariant 9).
 */
bool choiceIsValid(const DiceChoice& choice);

/**
 * @brief Serialise @p record into @p bytes, which must hold `PRESET_RECORD_BYTES`.
 *
 * @return how many bytes were written, or 0 when @p capacity is too small.
 */
size_t writePresets(const PresetRecord& record, uint8_t* bytes, size_t capacity);

/**
 * @brief Read a record back out of @p bytes.
 *
 * Every field is checked: the length, the version, each cursor against its bound, and each choice
 * that is in use against the catalog. One bad field discards the whole record, because a repaired
 * record is a choice the player never made (ADR-0038).
 *
 * @return false, leaving @p into untouched, when the bytes are not a record this firmware wrote.
 */
bool readPresets(const uint8_t* bytes, size_t size, PresetRecord& into);

/** @brief The record after a save, and the slot the choice went into. */
struct PresetSave {
    PresetRecord record;
    uint8_t      slot;
};

/** @brief Save @p choice into the next slot, overwriting the oldest once all four are full. */
PresetSave savePreset(const PresetRecord& record, const DiceChoice& choice);

/** @brief The record after a recall, and what to roll. */
struct PresetRecall {
    PresetRecord record;
    DiceChoice   choice;
    /** @brief A slot was recalled. False when nothing is saved, and @p choice is then untouched. */
    bool         found;
    /** @brief The slot @p choice came out of; only meaningful when `found`. */
    uint8_t      slot;
};

/**
 * @brief Step to the next saved slot.
 *
 * @param current what the device is set to now, returned unchanged when no slot is saved, so a
 * hold on a device that has saved nothing changes nothing rather than rolling something arbitrary.
 */
PresetRecall recallPreset(const PresetRecord& record, const DiceChoice& current);

// What the band says about a slot, in its own words. A save and a recall are the only things the
// holds do and nothing on screen advertises them, so the band saying what happened is the whole
// feedback the player gets (ADR-0038). Slots are shown counting from 1, because the player is
// being told which of four presets this is and not an index into an array.
constexpr char PRESET_SAVED_PREFIX[] = "saved as ";
constexpr char PRESET_RECALLED_PREFIX[] = "preset ";
constexpr char PRESET_NONE_SAVED[] = "no presets yet";

/** @brief The band that reports a save of @p slot, counted from 0. */
ResultsBand savedBand(uint8_t slot);

/** @brief The band that reports a recall of @p slot, counted from 0. */
ResultsBand recalledBand(uint8_t slot);

/** @brief The band that reports a hold that found nothing to recall. */
ResultsBand noPresetsBand();

#endif // PRESET_STORE_H

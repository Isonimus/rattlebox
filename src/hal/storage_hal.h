#ifndef STORAGE_HAL_H
#define STORAGE_HAL_H

#include <stddef.h>
#include <stdint.h>

#include "startable.h"

/**
 * @brief What one read of the store found.
 *
 * Nothing stored and a read that failed are different states and must not be conflated: the
 * Launcher offers the player a command that formats the NVS partition, so an empty store is a
 * state the device is reached in on purpose, while a store that cannot be read is a fault to be
 * loud about (ADR-0038).
 */
struct StoredRecord {
    /** @brief Bytes written into the caller's buffer; 0 when nothing was stored. */
    size_t size;
    /** @brief The read failed, and `lastError()` says why. `size` is then meaningless. */
    bool   failed;
};

/**
 * @brief The one record this firmware keeps across a power cycle, as bytes.
 *
 * It is bytes and a size and nothing more: what the record means belongs to `preset_store.h`,
 * which is pure and tested on the host, and keeping the die catalog out of here is what lets the
 * store be tested with no flash in it (slice 0039).
 *
 * There is one record rather than a key per field, because one record is one NVS entry and so one
 * compare-and-write however many fields changed.
 */
class StorageHAL : public Startable {
public:
    /**
     * @brief Read the stored record into @p into, at most @p capacity bytes.
     *
     * A record longer than @p capacity is a record this firmware did not write and cannot read, so
     * it reports nothing stored rather than a truncated record.
     */
    virtual StoredRecord readRecord(uint8_t* into, size_t capacity) = 0;

    /** @brief Write @p size bytes as the stored record. False on failure, with `lastError()` set. */
    virtual bool writeRecord(const uint8_t* from, size_t size) = 0;

    /** @brief Why the last read or write failed; only meaningful after one reported failure. */
    virtual const char* lastError() const = 0;
};

#endif // STORAGE_HAL_H

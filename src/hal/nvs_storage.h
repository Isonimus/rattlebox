#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include <nvs.h>

#include "storage_hal.h"

/**
 * @brief The record kept in this firmware's own NVS namespace.
 *
 * Shared by both boards rather than written once per board: both are ESP32 and NVS is identical on
 * them, so a second copy would be the duplication invariant 1 exists to catch (slice 0039). That
 * also puts this file under invariant 1, which is where it belongs: a change here has to build for
 * both boards.
 *
 * It does not call `nvs_flash_init`, because the Arduino core already did before `setup()` ran, and
 * it never erases the partition on an error: the Launcher keeps its own boot state in those same
 * 20 KB, and a firmware that formats a shared partition to fix its own record destroys another
 * program's state (ADR-0038).
 */
class NvsStorage : public StorageHAL {
public:
    bool begin() override;
    const char* failureReason() const override { return _failureReason; }
    const char* description() const override { return _description; }

    StoredRecord readRecord(uint8_t* into, size_t capacity) override;
    bool writeRecord(const uint8_t* from, size_t size) override;
    const char* lastError() const override { return _lastError; }

private:
    // This firmware's own namespace and key, spelled once. The partition is shared with whatever
    // else has run on the board, so a namespace of our own is what keeps the keys from colliding
    // with the Launcher's (ADR-0038).
    static constexpr char NVS_NAMESPACE[] = "rattlebox";
    static constexpr char RECORD_KEY[] = "presets";
    // "namespace " + the name + ", key " + the key, and the terminator.
    static constexpr size_t DESCRIPTION_CAPACITY =
        sizeof("namespace ") + sizeof(NVS_NAMESPACE) + sizeof(", key ") + sizeof(RECORD_KEY);

    nvs_handle_t _handle = 0;
    bool         _open = false;
    const char*  _failureReason = "";
    const char*  _lastError = "";
    // Built at begin() from the two names above, so the boot line cannot drift from what was opened.
    char         _description[DESCRIPTION_CAPACITY] = "";
};

#endif // NVS_STORAGE_H

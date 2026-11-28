#include "nvs_storage.h"

#include <stdio.h>

#include <esp_err.h>

bool NvsStorage::begin() {
    snprintf(_description, sizeof _description, "namespace %s, key %s", NVS_NAMESPACE, RECORD_KEY);

    // READWRITE creates the namespace when it is not there yet, which is the state a device with a
    // formatted NVS boots in. A namespace that cannot be opened is a real failure: the core's own
    // nvs_flash_init() only logs when it fails, so this open is the first place the product can
    // learn that the partition is unusable, and it has to stop rather than run with presets that
    // silently never persist (ADR-0038).
    const esp_err_t opened = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &_handle);
    if (opened != ESP_OK) {
        _failureReason = esp_err_to_name(opened);
        return false;
    }
    _open = true;
    return true;
}

StoredRecord NvsStorage::readRecord(uint8_t* into, size_t capacity) {
    if (!_open) {
        _lastError = "the store was never opened";
        return StoredRecord{0, true};
    }

    // Asking with a null buffer reports the stored length, which is how a record of another length
    // is told from one this firmware can read before any of it is copied.
    size_t stored = 0;
    const esp_err_t measured = nvs_get_blob(_handle, RECORD_KEY, nullptr, &stored);
    if (measured == ESP_ERR_NVS_NOT_FOUND) {
        // Nothing has been written yet, or the player formatted the partition from the Launcher.
        // Both are normal states and neither is an error (ADR-0038).
        return StoredRecord{0, false};
    }
    if (measured != ESP_OK) {
        _lastError = esp_err_to_name(measured);
        return StoredRecord{0, true};
    }
    if (stored > capacity) {
        // A longer record is one another version or another firmware wrote. Reporting it as nothing
        // stored is what makes the caller fall back to its defaults; copying its first bytes would
        // hand it a record it would then have to guess the meaning of.
        return StoredRecord{0, false};
    }

    size_t read = stored;
    const esp_err_t got = nvs_get_blob(_handle, RECORD_KEY, into, &read);
    if (got != ESP_OK) {
        _lastError = esp_err_to_name(got);
        return StoredRecord{0, true};
    }
    return StoredRecord{read, false};
}

bool NvsStorage::writeRecord(const uint8_t* from, size_t size) {
    if (!_open) {
        _lastError = "the store was never opened";
        return false;
    }

    // ESP-IDF compares the stored bytes against these and returns without writing when they match,
    // so a save that changed nothing reaches no flash and needs no debounce here (ADR-0038).
    const esp_err_t written = nvs_set_blob(_handle, RECORD_KEY, from, size);
    if (written != ESP_OK) {
        _lastError = esp_err_to_name(written);
        return false;
    }

    // Until this returns, the write is in NVS's own buffers and a power cut loses it. The whole
    // point of the record is surviving a power cut, so the commit is part of the write and not a
    // later tidying step.
    const esp_err_t committed = nvs_commit(_handle);
    if (committed != ESP_OK) {
        _lastError = esp_err_to_name(committed);
        return false;
    }
    return true;
}

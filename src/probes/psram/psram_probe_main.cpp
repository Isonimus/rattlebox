#include <Arduino.h>
#include <esp_psram.h>

// ADR-0020's probe: the StickC's ESP32-PICO-V3-02 has PSRAM in its package, but the product's heap
// holds none of it, and the core logs what became of it before the UART log is attached. This asks
// the core and ESP-IDF directly: whether the core found PSRAM, whether ESP-IDF started it, and
// whether starting it by hand and running the core's memory test both succeed. If they do, the chip
// is sound and only the build leaves PSRAM out.

// ESP-IDF's PSRAM memory test, the one the core runs before it puts PSRAM on the heap. Arduino.h
// declares it without C linkage, so a C++ call to that declaration does not link; this binds a
// name of our own to the C symbol.
bool psramMemoryTest() asm("esp_psram_extram_test");

namespace {

constexpr uint32_t SERIAL_BAUD = 115200;
// The USB bridge enumerates before the monitor attaches; the first lines would be lost without it.
constexpr uint32_t MONITOR_ATTACH_MS = 2000;
constexpr uint32_t HALT_POLL_MS = 1000;

}  // namespace

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(MONITOR_ATTACH_MS);
    Serial.printf("[psram] core_found=%d idf_initialized=%d size=%u\n", psramFound() ? 1 : 0,
                  esp_psram_is_initialized() ? 1 : 0, static_cast<unsigned>(esp_psram_get_size()));
    if (esp_psram_is_initialized()) return;
    const esp_err_t initResult = esp_psram_init();
    Serial.printf("[psram] init by hand: %s\n", esp_err_to_name(initResult));
    if (initResult != ESP_OK) return;
    // It overwrites all of PSRAM, which is safe only because nothing has been put there.
    Serial.printf("[psram] memory test: %s\n", psramMemoryTest() ? "passed" : "FAILED");
}

void loop() { delay(HALT_POLL_MS); }

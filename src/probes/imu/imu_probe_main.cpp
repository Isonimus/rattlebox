#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>
#include <stdlib.h>

#include "../../boot/boot_steps.h"
#include "../../hal/hal.h"
#include "imu_wire.h"

// The IMU probe firmware (slice 0004): on each `R` from the host, record one window of IMU
// samples at the chip's fastest rate, printing nothing meanwhile, then send it framed as
// RBIMU ... RBEND. scripts/imu-probe.mjs drives it and analyses the windows.

#ifndef IMU_PROBE_OUTPUT_RATE_HZ
#error "the probe env sets IMU_PROBE_OUTPUT_RATE_HZ to its chip's fastest rate"
#endif

namespace {

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t HALT_POLL_MS = 1000;

// ±16 g is the top range of both chips, so a shake is measured rather than clipped as it
// was at ±8 g (slice 0003). ±2000 °/s held slice 0003's 828 °/s twist with room to spare.
constexpr ImuSettings PROBE_IMU_SETTINGS{16, 2000, IMU_PROBE_OUTPUT_RATE_HZ};
constexpr ImuFullScale PROBE_FULL_SCALE = fullScaleOf(PROBE_IMU_SETTINGS);
// Long enough for a pick-up or a few walking steps; at 1 kHz it is 80 KB, which the StickC
// holds only because this firmware has no canvas.
constexpr uint32_t WINDOW_SECONDS = 5;
constexpr size_t WINDOW_SAMPLES = static_cast<size_t>(IMU_PROBE_OUTPUT_RATE_HZ) * WINDOW_SECONDS;
constexpr int RECORD_REQUEST = 'R';
// Ten periods at 100 Hz, and far more at the probe's fast rates: a chip that stops raising
// data-ready without a bus error would otherwise hold the board in the recording loop for
// good, with the host seeing only a timeout.
constexpr uint32_t SAMPLE_STALL_TIMEOUT_MS = 100;
constexpr size_t BYTES_PER_KB = 1024;

constexpr int TEXT_LEFT_PX = 6;
constexpr int TEXT_TOP_PX = 8;
constexpr int TEXT_LINE_PX = 14;
constexpr int STATE_TEXT_SIZE = 2;

/**
 * @brief The recording, allocated once at boot so a window can never fail halfway for
 * want of memory, and so the boot log records how much room was left.
 */
class RecordingBuffer : public Startable {
public:
    bool begin() override {
        _largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        _samples = static_cast<PackedSample*>(malloc(WINDOW_SAMPLES * sizeof(PackedSample)));
        snprintf(_description, sizeof(_description), "%u samples, %u KB of a %u KB largest free block",
                 static_cast<unsigned>(WINDOW_SAMPLES), static_cast<unsigned>(byteCount() / BYTES_PER_KB),
                 static_cast<unsigned>(_largestFreeBlock / BYTES_PER_KB));
        return _samples != nullptr;
    }
    const char* failureReason() const override { return _description; }
    const char* description() const override { return _description; }

    PackedSample* samples() { return _samples; }
    static constexpr size_t byteCount() { return WINDOW_SAMPLES * sizeof(PackedSample); }

private:
    PackedSample* _samples = nullptr;
    size_t _largestFreeBlock = 0;
    char _description[80] = "";
};

HardwareHAL& hal = getHAL();
RecordingBuffer recording;
bool displayUp = false;

void showState(const char* state) {
    if (!displayUp) return;
    lgfx::LGFX_Device& tft = hal.display.getDevice();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(TEXT_LEFT_PX, TEXT_TOP_PX);
    tft.printf("%s IMU probe\n+-%u g +-%u dps %u Hz", hal.boardName, PROBE_IMU_SETTINGS.accelRangeG,
               PROBE_IMU_SETTINGS.gyroRangeDps, PROBE_IMU_SETTINGS.outputRateHz);
    tft.setTextSize(STATE_TEXT_SIZE);
    tft.setCursor(TEXT_LEFT_PX, TEXT_TOP_PX + TEXT_LINE_PX * 3);
    tft.print(state);
}

void logStarted(const BootStep& step) {
    Serial.printf("[boot] %s OK: %s\n", step.subsystem, step.part.description());
    if (&step.part == &hal.display) displayUp = true;
}

[[noreturn]] void haltOnFailure(const BootOutcome& outcome) {
    Serial.printf("[boot] %s FAILED: %s\n", outcome.failedStep->subsystem, outcome.failureReason);
    showState("boot FAILED");
    // Nothing may be recorded on hardware that is only partly up.
    for (;;) delay(HALT_POLL_MS);
}

/**
 * @brief Read and drop whatever sample is waiting, so the window starts on a fresh
 * conversion.
 *
 * Between windows nothing reads the chip for seconds, so its data-ready flag is already
 * set when a request arrives and the first sample was taken off the chip's grid, whenever
 * the request landed. On the StickC that made the first interval 1407 to 1624 µs in 6 of
 * 8 windows, read as a missed sample, while every later interval stayed under 1162 µs
 * (slice 0004).
 * @return false on an IMU bus error.
 */
bool discardWaitingSample() {
    bool available = false;
    ImuSample sample;
    if (!hal.imu.hasNewSample(available)) return false;
    return !available || hal.imu.read(sample);
}

/** @return false on an IMU bus error, after logging it; the window is then not sent. */
bool recordWindow() {
    PackedSample* samples = recording.samples();
    if (!discardWaitingSample()) {
        Serial.print("[probe] FAILED: IMU bus error before the first sample\n");
        return false;
    }
    size_t recorded = 0;
    uint32_t lastSampleMs = millis();
    while (recorded < WINDOW_SAMPLES) {
        bool available = false;
        ImuSample sample;
        if (!hal.imu.hasNewSample(available) || (available && !hal.imu.read(sample))) {
            Serial.printf("[probe] FAILED: IMU bus error after %u samples\n", static_cast<unsigned>(recorded));
            return false;
        }
        if (available) {
            samples[recorded++] = packSample(sample, PROBE_FULL_SCALE);
            lastSampleMs = millis();
        } else if (millis() - lastSampleMs > SAMPLE_STALL_TIMEOUT_MS) {
            Serial.printf("[probe] FAILED: no sample for %lu ms after %u samples\n",
                          static_cast<unsigned long>(SAMPLE_STALL_TIMEOUT_MS), static_cast<unsigned>(recorded));
            return false;
        }
    }
    return true;
}

void sendWindow() {
    Serial.printf("RBIMU %u %u %u %u %u\n", PROBE_IMU_SETTINGS.outputRateHz, PROBE_IMU_SETTINGS.accelRangeG,
                  PROBE_IMU_SETTINGS.gyroRangeDps, static_cast<unsigned>(WINDOW_SAMPLES),
                  static_cast<unsigned>(RecordingBuffer::byteCount()));
    const size_t sent = Serial.write(reinterpret_cast<const uint8_t*>(recording.samples()), RecordingBuffer::byteCount());
    if (sent != RecordingBuffer::byteCount()) {
        // The native USB drops what the host does not read within its TX timeout. No RBEND,
        // so the host cannot mistake the remains for a whole window.
        Serial.printf("\n[probe] only %u of %u bytes sent; window abandoned\n", static_cast<unsigned>(sent),
                      static_cast<unsigned>(RecordingBuffer::byteCount()));
        return;
    }
    Serial.print("RBEND\n");
}

} // namespace

void setup() {
    Serial.begin(SERIAL_BAUD);
    hal.imu.configure(PROBE_IMU_SETTINGS);

    // No input step: the probe is driven from the host, and the BMI270 starts the
    // Cardputer's shared I2C bus itself.
    const BootStep steps[] = {
        {"power", hal.power},
        {"display", hal.display},
        {"recording", recording},
        {"imu", hal.imu},
    };
    const BootOutcome outcome = runBootSteps(steps, sizeof(steps) / sizeof(steps[0]), logStarted);
    if (outcome.failedStep != nullptr) haltOnFailure(outcome);

    Serial.printf("[boot] %s IMU probe ready\n", hal.boardName);
    showState("ready");
}

void loop() {
    // Counted and reported once, as the screen capture does, so line noise cannot flood
    // the console ahead of a reply.
    unsigned ignoredBytes = 0;
    while (Serial.available() > 0) {
        if (Serial.read() != RECORD_REQUEST) {
            ignoredBytes++;
            continue;
        }
        showState("recording");
        const bool recorded = recordWindow();
        showState("sending");
        if (recorded) sendWindow();
        showState("ready");
    }
    if (ignoredBytes > 0) Serial.printf("[probe] ignored %u byte(s); send 'R' to record\n", ignoredBytes);
}

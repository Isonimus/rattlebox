#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <stdlib.h>
#include <utility>

#include "../../boot/boot_steps.h"
#include "../../debug/screen_capture.h"
#include "../../geometry/die_shape.h"
#include "../../geometry/rotation.h"
#include "../../hal/hal.h"
#include "../../render/die_drawing.h"
#include "../../render/die_view.h"
#include "frame_stats.h"

// The render probe firmware (slice 0006): at boot, time a frame of three spinning D20s in
// each push mode and die size, print each run's statistics, then hold the last frame for
// the screen capture. scripts/render-probe.mjs drives it.

namespace {

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t HALT_POLL_MS = 1000;
// Ten seconds of frames at 60 fps: enough orientations that the count of front faces and
// numerals has varied through its range, and enough frames that a 95th percentile is 30 of them.
constexpr size_t FRAMES_PER_RUN = 600;
constexpr size_t BYTES_PER_PIXEL = 2;
constexpr int CANVAS_COLOR_DEPTH = 16;

// docs/roadmap.md puts a D20 face at about 12 px across; 24 px shows what a larger,
// easier-to-read die would cost.
constexpr float EDGES_PX[] = {12.0f, 24.0f};
// The probe builds its dice in pixels, so they draw at a scale of one.
constexpr float SHAPE_IN_PIXELS = 1.0f;
constexpr uint8_t DIE_COUNT = 3;
// Spread across the 240 × 135 tray so no two dice overlap even at 24 px (about 46 px across).
constexpr Vec3 DIE_CENTRES_PX[DIE_COUNT] = {{48, 67, 0}, {120, 67, 0}, {192, 67, 0}};
// Each die tumbles about its own axis at its own rate, so their faces do not line up.
constexpr Vec3 SPIN_AXES[DIE_COUNT] = {{1, 2, 3}, {-2, 1, 1}, {1, -1, 2}};
constexpr float SPIN_RAD_PER_FRAME[DIE_COUNT] = {0.050f, 0.071f, 0.093f};
constexpr Rgb DIE_COLORS[DIE_COUNT] = {{235, 225, 200}, {200, 40, 40}, {40, 90, 210}};
constexpr Rgb TRAY_COLOR{20, 70, 40};
constexpr Rgb NUMERAL_COLOR{0, 0, 0};
constexpr float AMBIENT = 0.35f;
// Generous on purpose: numerals up to 60° from the camera, more than the roll slice is
// likely to show, so the cost measured is an upper bound.
constexpr float NUMERAL_MIN_FACING = 0.5f;

enum Phase : uint8_t { CLEAR, GEOMETRY, RASTER, NUMERALS, PUSH, FRAME, PHASE_COUNT };
constexpr const char* PHASE_NAMES[PHASE_COUNT] = {"clear", "geometry", "raster", "numerals", "push", "frame"};

enum class PushMode : uint8_t { Blocking, Overlapped };

constexpr int TEXT_LEFT_PX = 6;
constexpr int TEXT_TOP_PX = 8;

/**
 * @brief A 16-bit, full-screen canvas.
 *
 * Constructed at file scope, so the constructor must not touch the display: the board's
 * display object lives in another file and may not be constructed yet.
 */
class Canvas : public Startable {
public:
    explicit Canvas(DisplayHAL& display) : _display(display) {}

    bool begin() override {
        lgfx::LGFX_Device& tft = _display.getDevice();
        _sprite.setColorDepth(CANVAS_COLOR_DEPTH);
        return _sprite.createSprite(tft.width(), tft.height()) != nullptr;
    }
    const char* failureReason() const override { return "no heap for the frame buffer"; }
    const char* description() const override { return "16-bit, full screen"; }

    void release() { _sprite.deleteSprite(); }
    LGFX_Sprite& sprite() { return _sprite; }
    static size_t byteCount(lgfx::LGFX_Device& tft) {
        return static_cast<size_t>(tft.width()) * tft.height() * BYTES_PER_PIXEL;
    }

private:
    DisplayHAL& _display;
    LGFX_Sprite _sprite;
};

/** @brief Every phase's duration for every frame of a run, allocated once at boot. */
class FrameTimes : public Startable {
public:
    bool begin() override {
        _durationsUs = static_cast<uint32_t*>(malloc(FRAMES_PER_RUN * PHASE_COUNT * sizeof(uint32_t)));
        return _durationsUs != nullptr;
    }
    const char* failureReason() const override { return "no heap for the frame timings"; }
    const char* description() const override { return "every phase of every frame of a run"; }

    /** @brief The durations of one phase, contiguous so they can be sorted in place. */
    uint32_t* phase(Phase phase) { return _durationsUs + phase * FRAMES_PER_RUN; }

private:
    uint32_t* _durationsUs = nullptr;
};

HardwareHAL& hal = getHAL();
Canvas front(hal.display);
Canvas back(hal.display);
FrameTimes frameTimes;
DieView views[DIE_COUNT];
bool displayUp = false;
const Lighting LIGHTING{normalized({-0.3f, 0.4f, 1.0f}), AMBIENT};

uint16_t packed(Rgb color) { return shadeRgb565(color, 1.0f); }

uint32_t elapsedUs(int64_t fromUs, int64_t toUs) { return static_cast<uint32_t>(toUs - fromUs); }

/** @brief Clear, pose and draw the scene into @p canvas, recording each phase's duration for @p frame. */
void drawScene(LGFX_Sprite& canvas, const DieShape& shape, size_t frame) {
    const int64_t start = esp_timer_get_time();
    canvas.fillScreen(packed(TRAY_COLOR));
    const int64_t cleared = esp_timer_get_time();
    for (uint8_t die = 0; die < DIE_COUNT; die++) {
        const Rotation orientation = rotationAbout(SPIN_AXES[die], SPIN_RAD_PER_FRAME[die] * frame);
        // Plain numerals: this probe measures what a frame of three D20s costs, and a D20's
        // numerals mean themselves. A percentile body would label the same geometry differently
        // without changing a single drawn pixel's cost (ADR-0034).
        viewDie(shape, Numbering::Plain, SHAPE_IN_PIXELS, orientation, DIE_CENTRES_PX[die], DIE_COLORS[die], LIGHTING,
                NUMERAL_MIN_FACING, views[die]);
    }
    const int64_t posed = esp_timer_get_time();
    for (const DieView& view : views) drawDieFaces(canvas, view);
    const int64_t filled = esp_timer_get_time();
    for (const DieView& view : views) drawDieNumerals(canvas, view, packed(NUMERAL_COLOR));
    const int64_t numbered = esp_timer_get_time();

    frameTimes.phase(CLEAR)[frame] = elapsedUs(start, cleared);
    frameTimes.phase(GEOMETRY)[frame] = elapsedUs(cleared, posed);
    frameTimes.phase(RASTER)[frame] = elapsedUs(posed, filled);
    frameTimes.phase(NUMERALS)[frame] = elapsedUs(filled, numbered);
}

/**
 * @brief Draw, then push and wait for the bus: the frame is the sum of the phases.
 */
void runBlocking(const DieShape& shape) {
    lgfx::LGFX_Device& tft = hal.display.getDevice();
    int64_t lastFrameEnd = esp_timer_get_time();
    for (size_t frame = 0; frame < FRAMES_PER_RUN; frame++) {
        drawScene(front.sprite(), shape, frame);
        const int64_t drawn = esp_timer_get_time();
        front.sprite().pushSprite(&tft, 0, 0);
        const int64_t pushed = esp_timer_get_time();
        frameTimes.phase(PUSH)[frame] = elapsedUs(drawn, pushed);
        frameTimes.phase(FRAME)[frame] = elapsedUs(lastFrameEnd, pushed);
        lastFrameEnd = pushed;
    }
}

/** @brief Start sending @p canvas to the panel by DMA and return without waiting for it. */
void startDmaPush(lgfx::LGFX_Device& tft, LGFX_Sprite& canvas) {
    // swap565_t is the sprite's own byte order, so LovyanGFX hands the buffer to DMA as it
    // is instead of converting it through a bounce buffer.
    tft.pushImageDMA(0, 0, canvas.width(), canvas.height(), static_cast<const lgfx::swap565_t*>(canvas.getBuffer()));
}

/**
 * @brief Draw into one canvas while DMA sends the other: the push phase is only the wait
 * for the previous push plus starting the next, and the frame is about the larger of the
 * drawing and the bus.
 *
 * The bus transaction stays open for the whole run: LovyanGFX's endWrite waits for the
 * bus to go idle, so closing it after each push would quietly make every push blocking
 * (ADR-0007).
 */
void runOverlapped(const DieShape& shape) {
    lgfx::LGFX_Device& tft = hal.display.getDevice();
    LGFX_Sprite* drawing = &back.sprite();
    LGFX_Sprite* sending = &front.sprite();
    tft.startWrite();
    int64_t lastFrameEnd = esp_timer_get_time();
    for (size_t frame = 0; frame < FRAMES_PER_RUN; frame++) {
        drawScene(*drawing, shape, frame);
        const int64_t drawn = esp_timer_get_time();
        tft.waitDMA();
        startDmaPush(tft, *drawing);
        const int64_t started = esp_timer_get_time();
        frameTimes.phase(PUSH)[frame] = elapsedUs(drawn, started);
        frameTimes.phase(FRAME)[frame] = elapsedUs(lastFrameEnd, started);
        lastFrameEnd = started;
        std::swap(drawing, sending);
    }
    tft.waitDMA();
    tft.endWrite();
}

const char* modeName(PushMode mode) { return mode == PushMode::Blocking ? "blocking" : "overlapped"; }

void reportRun(PushMode mode, float edgePx) {
    Serial.printf("[render] result %s edge=%u frames=%u", modeName(mode), static_cast<unsigned>(edgePx),
                  static_cast<unsigned>(FRAMES_PER_RUN));
    for (uint8_t phase = 0; phase < PHASE_COUNT; phase++) {
        const PhaseStats stats = summarizeDurations(frameTimes.phase(static_cast<Phase>(phase)), FRAMES_PER_RUN);
        Serial.printf(" %s=%lu/%lu/%lu", PHASE_NAMES[phase], static_cast<unsigned long>(stats.medianUs),
                      static_cast<unsigned long>(stats.p95Us), static_cast<unsigned long>(stats.maxUs));
    }
    Serial.print("\n");
}

void runAllSizes(PushMode mode) {
    for (float edgePx : EDGES_PX) {
        const DieShape shape = makeD20(edgePx);
        if (mode == PushMode::Blocking) {
            runBlocking(shape);
        } else {
            runOverlapped(shape);
        }
        reportRun(mode, edgePx);
    }
}

/**
 * @brief The overlapped runs, if a second canvas fits; if not, that is the finding, so it
 * is reported with the numbers and the probe goes on.
 */
void runOverlappedIfItFits() {
    lgfx::LGFX_Device& tft = hal.display.getDevice();
    const size_t needed = Canvas::byteCount(tft);
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    if (!back.begin()) {
        Serial.printf("[render] unavailable overlapped: a second canvas needs %u bytes, largest free DMA block %u\n",
                      static_cast<unsigned>(needed), static_cast<unsigned>(largest));
        return;
    }
    Serial.printf("[render] second canvas: %u bytes from a largest free DMA block of %u\n",
                  static_cast<unsigned>(needed), static_cast<unsigned>(largest));
    runAllSizes(PushMode::Overlapped);
    back.release();
}

/**
 * @brief Leave a frame on the panel that the front canvas also holds: the largest die, mid-run,
 * where the three no longer share an orientation.
 *
 * Sent by the overlapped mode's DMA push, not pushSprite. The capture reads the canvas, not the
 * panel, so the panel matching the PNG is what shows the DMA path sends the pixels unchanged.
 */
void showFinalFrame() {
    constexpr size_t EDGE_COUNT = sizeof(EDGES_PX) / sizeof(EDGES_PX[0]);
    lgfx::LGFX_Device& tft = hal.display.getDevice();
    drawScene(front.sprite(), makeD20(EDGES_PX[EDGE_COUNT - 1]), FRAMES_PER_RUN / 2);
    tft.startWrite();
    startDmaPush(tft, front.sprite());
    tft.waitDMA();
    tft.endWrite();
}

void logStarted(const BootStep& step) {
    Serial.printf("[boot] %s OK: %s\n", step.subsystem, step.part.description());
    if (&step.part == &hal.display) displayUp = true;
}

[[noreturn]] void haltOnFailure(const BootOutcome& outcome) {
    Serial.printf("[boot] %s FAILED: %s\n", outcome.failedStep->subsystem, outcome.failureReason);
    if (displayUp) {
        lgfx::LGFX_Device& tft = hal.display.getDevice();
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(TEXT_LEFT_PX, TEXT_TOP_PX);
        tft.print("render probe: boot FAILED");
    }
    // Nothing may be measured on hardware that is only partly up.
    for (;;) delay(HALT_POLL_MS);
}

} // namespace

void setup() {
    Serial.begin(SERIAL_BAUD);

    // No input or IMU: the probe runs at boot and nothing in it reads either.
    const BootStep steps[] = {
        {"power", hal.power},
        {"display", hal.display},
        {"canvas", front},
        {"timings", frameTimes},
    };
    const BootOutcome outcome = runBootSteps(steps, sizeof(steps) / sizeof(steps[0]), logStarted);
    if (outcome.failedStep != nullptr) haltOnFailure(outcome);
    Serial.printf("[boot] %s render probe ready\n", hal.boardName);

    runAllSizes(PushMode::Blocking);
    runOverlappedIfItFits();
    showFinalFrame();
    Serial.print("[render] done\n");
}

void loop() {
    serviceScreenCapture(front.sprite(), Serial);
}

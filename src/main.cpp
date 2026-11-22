#include <Arduino.h>
#include <esp_heap_caps.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <math.h>
#include <utility>

#include "app/frame_pacer.h"
#include "app/hardware_source.h"
#include "app/serial_commands.h"
#include "boot/boot_steps.h"
#include "debug/screen_capture.h"
#include "firmware_version.h"
#include "geometry/quaternion.h"
#include "hal/hal.h"
#include "render/band_drawing.h"
#include "render/die_drawing.h"
#include "render/die_view.h"
#include "render/results_band.h"
#include "render/tray_layout.h"
#include "roll/dice_choice.h"
#include "roll/dice_roll.h"
#include "roll/shake_roll.h"
#include "sound/clack_loudness.h"
#include "sound/hit_finder.h"

// The product: dice rolled by the device's own motion (slice 0012) or launched from rest by the
// controls and the serial port (slice 0011), stepped by the physics at 60 fps, and their results
// shown once every die locks; each hit in the physics clacks (slice 0013).

namespace {

constexpr uint32_t SERIAL_BAUD = 115200;
// A roll's report, three dice and the frame line, is about 330 bytes. Past the StickC's 128-byte
// UART FIFO, printing blocked the loop for about 14 ms at 115200 baud and made the next frame late
// (slice 0012); a buffer of a few reports lets the UART send them while the loop runs on.
constexpr size_t SERIAL_TX_BUFFER_BYTES = 1024;
constexpr uint32_t HALT_POLL_MS = 1000;
// Measured by the IMU probe (ADR-0005): shakes reach 9.8 g, so +-8 g clips them, and a shake
// is told from handling as well at 100 Hz as at the chips' fastest rates.
constexpr ImuSettings PRODUCT_IMU_SETTINGS{16, 2000, 100};
// How often the frame's waits read the IMU: a tenth of its 10 ms period, so no sample is replaced by
// the next before it is read, and a sample's timestamp, its read time, is late by at most this.
constexpr uint32_t IMU_POLL_US = 1000;
constexpr float DEG_PER_RAD = 180.0f / static_cast<float>(M_PI);

// Four 1/240 s steps make one 1/60 s frame (ADR-0007, ADR-0009). The physics follows frames, not
// the clock: a slow frame slows the dice rather than skipping steps the fairness test never ran.
constexpr uint8_t STEPS_PER_FRAME = 4;
// A roll that has not locked by now has a die the physics never brings to rest: the fairness
// test's limit, which no roll of its tens of thousands reached.
constexpr float ROLL_LIMIT_S = 20.0f;
// How long after a lock the clacks are counted as the dice lying at rest: long past the 0.037 s
// after which a resting die last tipped (slice 0008), and short enough for a person to hold still.
constexpr float REST_REPORT_S = 2.0f;

constexpr int CANVAS_COLOR_DEPTH = 16;
// The render probe's palette (slice 0006): one colour per die, so the band's results match them.
constexpr Rgb DIE_COLORS[MAX_DICE] = {{235, 225, 200}, {200, 40, 40}, {40, 90, 210}};
constexpr Rgb TRAY_COLOR{20, 70, 40};
constexpr Rgb NUMERAL_COLOR{0, 0, 0};
constexpr Rgb BAND_COLOR{0, 0, 0};
constexpr Rgb BAND_TEXT_COLOR{255, 255, 255};
constexpr float AMBIENT = 0.35f;
// The render probe's bound, 60° from the camera, which ADR-0007 left to this slice to settle.
constexpr float NUMERAL_MIN_FACING = 0.5f;

constexpr int TEXT_LEFT_PX = 6;
constexpr int TEXT_TOP_PX = 8;

/**
 * @brief A full-screen 16-bit canvas, allocated once at boot.
 *
 * Constructed at file scope, so the constructor must not touch the display: the board's
 * display object lives in another file and may not be constructed yet.
 */
class FrameCanvas : public Startable {
public:
    explicit FrameCanvas(DisplayHAL& display) : _display(display) {}

    bool begin() override {
        lgfx::LGFX_Device& tft = _display.getDevice();
        _sprite.setColorDepth(CANVAS_COLOR_DEPTH);
        // The tray's depth is derived from this size (ADR-0016): on another screen its walls would not
        // be where the physics puts them, so the product stops rather than draw a tray that lies.
        if (tft.width() != SCREEN_WIDTH_PX || tft.height() != SCREEN_HEIGHT_PX) {
            _failureReason = "screen is not the 240x135 the tray is laid out for";
            return false;
        }
        _failureReason = "no heap for the frame buffer";
        return _sprite.createSprite(tft.width(), tft.height()) != nullptr;
    }
    const char* failureReason() const override { return _failureReason; }
    const char* description() const override { return "16-bit, full screen"; }

    LGFX_Sprite& sprite() { return _sprite; }

private:
    DisplayHAL& _display;
    LGFX_Sprite _sprite;
    const char* _failureReason = nullptr;
};

/** @brief What a roll's frames cost, kept as running figures: the StickC has no heap to spare for a buffer. */
struct FrameCosts {
    uint32_t frames;
    uint64_t workSumUs;
    uint32_t longestWorkUs;
    uint32_t longestPhysicsUs;
    uint32_t lateFrames;
    // The IMU's samples over the roll, by their timestamps, so a sample lost to a long gap between
    // reads shows as a rate under the chip's (slice 0012).
    uint32_t imuSamples;
    uint32_t firstImuUs;
    uint32_t lastImuUs;
    uint32_t longestImuGapUs;
};

HardwareHAL& hal = getHAL();
FrameCanvas front(hal.display);
FrameCanvas back(hal.display);
HardwareSource randomness;
bool displayUp = false;

const DieShape D6_SHAPE = makeD6(D6_EDGE_MM);
const DieShape D20_SHAPE = makeD20(D20_EDGE_MM);
const Lighting LIGHTING{normalized(Vec3{-0.3f, 0.4f, 1.0f}), AMBIENT};

DiceChoice choice = BOOT_CHOICE;
World world{};
DieResult results[MAX_DICE];
ShakeRoll shakeRoll(PRODUCT_KICK);
uint32_t rollSteps = 0;
uint16_t rollNudges = 0;
// A shake the detector reads twice is one roll kicked twice (ADR-0010); the count shows how often.
uint8_t rollDetections = 0;
bool rollReported = false;
FrameCosts costs{};
HitFinder hitFinder;
uint32_t rollClacks = 0;
// The clacks over REST_REPORT_S after a lock, where the dice lie still and none should play.
uint32_t restClacks = 0;
uint32_t restSteps = 0;
bool restReported = false;

LGFX_Sprite* drawing = nullptr;
LGFX_Sprite* sending = nullptr;
uint32_t frameDeadlineUs = 0;

uint16_t packed(Rgb color) { return shadeRgb565(color, 1.0f); }

const DieShape& shapeOf(DieType type) { return type == DieType::D20 ? D20_SHAPE : D6_SHAPE; }

void startRollCounters() {
    rollSteps = 0;
    rollNudges = 0;
    rollDetections = 0;
    rollReported = false;
    costs = {};
    rollClacks = 0;
    restClacks = 0;
    restSteps = 0;
    restReported = false;
    hal.audio.takeLongestRenderUs();
}

/** @brief The heap now and at its lowest since boot, and the largest block a DMA buffer could take (ADR-0007). */
void reportHeap() {
    Serial.printf("[heap] free=%u min_free=%u largest_dma=%u\n",
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
}

/**
 * @brief Clack for each hit of the step just taken that is loud enough to hear, and count it: as the
 * roll's until its lock is reported, then as the rest's. The rest's steps are counted here too, so
 * they are exactly the steps whose hits it holds, starting with the first step after the lock's frame.
 */
void playHits() {
    if (rollReported && !restReported) restSteps++;
    float impulses[MAX_HITS_PER_STEP];
    const uint8_t hits = hitFinder.find(world, impulses);
    for (uint8_t hit = 0; hit < hits; hit++) {
        const float loudness = clackLoudness(impulses[hit]);
        if (loudness == 0) continue;
        hal.audio.clack(loudness);
        if (!rollReported) {
            rollClacks++;
        } else if (!restReported) {
            restClacks++;
        }
    }
}

/** @brief Once REST_REPORT_S has passed since the lock, say how many clacks played meanwhile. */
void reportRest() {
    if (!rollReported || restReported || restSteps < stepsIn(REST_REPORT_S)) return;
    Serial.printf("[sound] rest clacks=%lu seconds=%.1f\n", static_cast<unsigned long>(restClacks), restSteps * PHYSICS_STEP_S);
    restReported = true;
}

void launch() {
    world.shape = &shapeOf(choice.type);
    shakeRoll.launch(world, results, choice.count, randomness);
    startRollCounters();
    Serial.printf("[roll] launch %s x%u\n", dieTypeName(choice.type), static_cast<unsigned>(choice.count));
}

/** @brief How far the level in force is tilted from the glass's normal, the turn's own angle. */
float levelTiltDeg() {
    const float w = fabsf(shakeRoll.level().w);
    return 2 * acosf(w < 1 ? w : 1) * DEG_PER_RAD;
}

void onShake() {
    // A roll's first detection starts its counters; a second kick from the same shake does not, so
    // its seconds count from the first, as the fairness test counts them.
    if (rollReported || rollDetections == 0) startRollCounters();
    rollDetections++;
    Serial.printf("[roll] shake %s x%u", dieTypeName(choice.type), static_cast<unsigned>(world.dieCount));
    if (shakeRoll.hasLevel()) {
        Serial.printf(" level_tilt_deg=%.1f\n", levelTiltDeg());
    } else {
        Serial.printf(" level_tilt_deg=none\n");
    }
}

void recordImuSample(uint32_t timestampUs) {
    if (rollReported) return;
    if (costs.imuSamples == 0) {
        costs.firstImuUs = timestampUs;
    } else if (timestampUs - costs.lastImuUs > costs.longestImuGapUs) {
        costs.longestImuGapUs = timestampUs - costs.lastImuUs;
    }
    costs.lastImuUs = timestampUs;
    costs.imuSamples++;
}

void press(InputAction action) {
    const ChoiceChange change = applyAction(choice, action);
    if (!change.changed) return;
    choice = change.choice;
    launch();
}

bool takeCommand(int byte) {
    switch (serialCommandFor(byte)) {
        case SerialCommand::Roll: launch(); return true;
        case SerialCommand::PressPrimary: press(InputAction::PRIMARY); return true;
        case SerialCommand::PressSecondary: press(InputAction::SECONDARY); return true;
        case SerialCommand::None: break;
    }
    return false;
}

/** @brief The numerals of @p count dice, comma-separated, for the verify script to read. */
void printNumerals(const char* label, const uint8_t* numerals, uint8_t count) {
    Serial.printf(" %s=", label);
    for (uint8_t die = 0; die < count; die++) Serial.printf(die == 0 ? "%u" : ",%u", static_cast<unsigned>(numerals[die]));
}

void reportRoll() {
    if (rollReported) return;
    const float seconds = rollSteps * PHYSICS_STEP_S;
    if (!everyDieLocked(results, world.dieCount)) {
        if (seconds < ROLL_LIMIT_S) return;
        Serial.printf("[roll] FAILED: not every die locked within %.0f s\n", ROLL_LIMIT_S);
        rollReported = true;
        // Dice still moving are no rest to count clacks in.
        restReported = true;
        return;
    }
    uint8_t locked[MAX_DICE];
    uint8_t shown[MAX_DICE];
    for (uint8_t die = 0; die < world.dieCount; die++) {
        locked[die] = results[die].numeral;
        shown[die] = shownNumeral(world, die);
    }
    Serial.printf("[roll] locked %s x%u", dieTypeName(choice.type), static_cast<unsigned>(world.dieCount));
    printNumerals("results", locked, world.dieCount);
    printNumerals("shown", shown, world.dieCount);
    Serial.printf(" seconds=%.3f nudges=%u detections=%u\n", seconds, static_cast<unsigned>(rollNudges),
                  static_cast<unsigned>(rollDetections));
    Serial.printf("[frame] frames=%lu work_mean_us=%lu work_max_us=%lu physics_max_us=%lu late=%lu imu_samples=%lu "
                  "imu_span_us=%lu imu_gap_max_us=%lu\n",
                  static_cast<unsigned long>(costs.frames),
                  static_cast<unsigned long>(costs.frames == 0 ? 0 : costs.workSumUs / costs.frames),
                  static_cast<unsigned long>(costs.longestWorkUs), static_cast<unsigned long>(costs.longestPhysicsUs),
                  static_cast<unsigned long>(costs.lateFrames), static_cast<unsigned long>(costs.imuSamples),
                  static_cast<unsigned long>(costs.lastImuUs - costs.firstImuUs),
                  static_cast<unsigned long>(costs.longestImuGapUs));
    Serial.printf("[sound] clacks=%lu render_max_us=%lu\n", static_cast<unsigned long>(rollClacks),
                  static_cast<unsigned long>(hal.audio.takeLongestRenderUs()));
    reportHeap();
    rollReported = true;
}

void drawDice(LGFX_Sprite& canvas) {
    // Lowest first, so a die resting on another is drawn over it.
    uint8_t order[MAX_DICE];
    for (uint8_t die = 0; die < world.dieCount; die++) order[die] = die;
    for (uint8_t i = 1; i < world.dieCount; i++) {
        for (uint8_t j = i; j > 0 && world.dice[order[j]].position.z < world.dice[order[j - 1]].position.z; j--) {
            std::swap(order[j], order[j - 1]);
        }
    }
    DieView view;
    for (uint8_t i = 0; i < world.dieCount; i++) {
        const uint8_t die = order[i];
        const DieBody& body = world.dice[die];
        viewDie(*world.shape, TRAY_PX_PER_MM, toRotation(body.orientation), trayToScreenPx(body.position),
                DIE_COLORS[die], LIGHTING, NUMERAL_MIN_FACING, view);
        drawDieFaces(canvas, view);
        drawDieNumerals(canvas, view, packed(NUMERAL_COLOR));
    }
}

void drawFrame(LGFX_Sprite& canvas) {
    canvas.fillScreen(packed(TRAY_COLOR));
    drawDice(canvas);
    const BandColors colors{packed(BAND_COLOR), packed(BAND_TEXT_COLOR), packed(NUMERAL_COLOR),
                            {packed(DIE_COLORS[0]), packed(DIE_COLORS[1]), packed(DIE_COLORS[2])}};
    // The strip is the band's own (ADR-0016): the type while the dice fly, the results once they lock.
    const char* typeName = dieTypeName(choice.type);
    if (!rollReported || !everyDieLocked(results, world.dieCount)) {
        drawResultsBand(canvas, typeBand(typeName), colors);
        return;
    }
    uint8_t numerals[MAX_DICE];
    for (uint8_t die = 0; die < world.dieCount; die++) numerals[die] = results[die].numeral;
    drawResultsBand(canvas, resultsBand(typeName, numerals, world.dieCount), colors);
}

/** @brief Start sending @p canvas to the panel by DMA and return without waiting for it. */
void startDmaPush(lgfx::LGFX_Device& tft, LGFX_Sprite& canvas) {
    // swap565_t is the sprite's own byte order, so LovyanGFX hands the buffer to DMA as it
    // is instead of converting it through a bounce buffer (ADR-0007).
    tft.pushImageDMA(0, 0, canvas.width(), canvas.height(), static_cast<const lgfx::swap565_t*>(canvas.getBuffer()));
}

void logStarted(const BootStep& step) {
    Serial.printf("[boot] %s OK: %s\n", step.subsystem, step.part.description());
    if (&step.part == &hal.display) displayUp = true;
}

/** @brief Show @p subsystem's failure on the screen, if it is up, and stop for good. */
[[noreturn]] void halt(const char* subsystem, const char* reason) {
    if (displayUp) {
        lgfx::LGFX_Device& tft = hal.display.getDevice();
        // A push in flight would draw over the message.
        tft.waitDMA();
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(TEXT_LEFT_PX, TEXT_TOP_PX);
        tft.printf("%s FAILED\n\n%s", subsystem, reason);
    }
    for (;;) delay(HALT_POLL_MS);
}

[[noreturn]] void haltOnFailure(const BootOutcome& outcome) {
    Serial.printf("[boot] %s FAILED: %s\n", outcome.failedStep->subsystem, outcome.failureReason);
    // Nothing later may run on hardware that is only partly up.
    halt(outcome.failedStep->subsystem, outcome.failureReason);
}

[[noreturn]] void haltOnImuFailure(const char* reason) {
    Serial.printf("[imu] FAILED: %s\n", reason);
    // Every roll from here on would be moved by a reading the bus may have garbled, or by none.
    halt("imu", reason);
}

/** @brief Read the IMU's sample if it has a new one, and roll by it. */
void pollImu() {
    bool available = false;
    if (!hal.imu.hasNewSample(available)) haltOnImuFailure("bus error on the data-ready check");
    if (!available) return;
    ImuSample sample{};
    if (!hal.imu.read(sample)) haltOnImuFailure("bus error on a sample read");
    if (shakeRoll.addSample(sample, world, results, randomness)) onShake();
    recordImuSample(sample.timestampUs);
}

/** @brief Wait @p waitUs, reading the IMU every IMU_POLL_US meanwhile; with nothing to wait, read it once. */
void waitReadingImu(uint32_t waitUs) {
    const uint32_t startUs = micros();
    for (;;) {
        pollImu();
        const uint32_t elapsedUs = micros() - startUs;
        if (elapsedUs >= waitUs) return;
        const uint32_t remainingUs = waitUs - elapsedUs;
        delayMicroseconds(remainingUs < IMU_POLL_US ? remainingUs : IMU_POLL_US);
    }
}

/**
 * @brief LovyanGFX's waitDMA, reading the IMU meanwhile: a push takes 13 ms of the frame's 16.7
 * (ADR-0007). It spins rather than sleeps between reads, so the push's end is seen within one read,
 * which took up to 1.3 ms on the StickC, not a sleep and a read.
 */
void waitDmaReadingImu(lgfx::LGFX_Device& tft) {
    uint32_t lastPollUs = micros() - IMU_POLL_US;
    while (tft.dmaBusy()) {
        if (micros() - lastPollUs < IMU_POLL_US) continue;
        lastPollUs = micros();
        pollImu();
    }
}

} // namespace

void setup() {
    // Before begin(), which allocates it. Without it, every report would stall the frame loop; the
    // abort's backtrace is the one failure report there is before the display is up. Both boards'
    // Serial (HardwareSerial, HWCDC) return the size asked for on success, and 0 on failure.
    if (Serial.setTxBufferSize(SERIAL_TX_BUFFER_BYTES) != SERIAL_TX_BUFFER_BYTES) abort();
    Serial.begin(SERIAL_BAUD);
    // First, so a boot that fails still says which build it was (ADR-0021).
    Serial.printf("[boot] Rattlebox %s on %s\n", RATTLEBOX_VERSION, hal.boardName);
    hal.imu.configure(PRODUCT_IMU_SETTINGS);

    // Power first: the StickC switches itself off unless its HOLD pin is latched, and
    // the latch cannot fail, so nothing is lost by bringing the display up second.
    const BootStep steps[] = {
        {"power", hal.power},   {"display", hal.display}, {"front canvas", front}, {"back canvas", back},
        {"input", hal.input},   {"imu", hal.imu},         {"audio", hal.audio}, {"rng", randomness},
    };
    const BootOutcome outcome = runBootSteps(steps, sizeof(steps) / sizeof(steps[0]), logStarted);
    if (outcome.failedStep != nullptr) haltOnFailure(outcome);
    // Every part is up, so nothing allocates its buffers later: this is the heap the product runs on.
    reportHeap();

    drawing = &back.sprite();
    sending = &front.sprite();
    // The bus transaction stays open for good: LovyanGFX's endWrite waits for the bus to go idle,
    // so closing it after each push would quietly make every push blocking (ADR-0007).
    hal.display.getDevice().startWrite();
    Serial.printf("[boot] %s ready\n", hal.boardName);
    launch();
    frameDeadlineUs = micros() + FRAME_PERIOD_US;
}

void loop() {
    const uint32_t frameStartUs = micros();
    // The canvas last pushed holds the whole frame on the panel; nothing draws into it this frame.
    serviceScreenCapture(*sending, Serial, takeCommand);
    hal.input.update();
    press(hal.input.getAction());

    // The IMU is read before each step as well as in the waits, so each step moves the dice by the
    // latest reading, as the fairness test's replay does.
    uint32_t physicsUs = 0;
    for (uint8_t step = 0; step < STEPS_PER_FRAME; step++) {
        pollImu();
        const uint32_t stepStartUs = micros();
        rollNudges += shakeRoll.step(world, results, randomness, PRODUCT_REST_THRESHOLDS);
        physicsUs += micros() - stepStartUs;
        playHits();
        rollSteps++;
    }

    drawFrame(*drawing);
    lgfx::LGFX_Device& tft = hal.display.getDevice();
    waitDmaReadingImu(tft);
    startDmaPush(tft, *drawing);
    std::swap(drawing, sending);

    const uint32_t workUs = micros() - frameStartUs;
    const FramePace pace = paceFrame(frameDeadlineUs, micros());
    // A launch from this frame's input zeroed `costs` after frameStartUs was taken, so the new roll's
    // first frame includes a little of the old one's work: one frame of many, not worth a second clock.
    if (!rollReported) {
        costs.frames++;
        costs.workSumUs += workUs;
        if (workUs > costs.longestWorkUs) costs.longestWorkUs = workUs;
        if (physicsUs > costs.longestPhysicsUs) costs.longestPhysicsUs = physicsUs;
        if (pace.late) costs.lateFrames++;
    }
    reportRoll();
    reportRest();
    waitReadingImu(pace.waitUs);
    frameDeadlineUs = pace.nextDeadlineUs;
}

#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <utility>

#include "app/frame_pacer.h"
#include "app/hardware_source.h"
#include "app/serial_commands.h"
#include "boot/boot_steps.h"
#include "debug/screen_capture.h"
#include "geometry/quaternion.h"
#include "hal/hal.h"
#include "render/band_drawing.h"
#include "render/die_drawing.h"
#include "render/die_view.h"
#include "render/results_band.h"
#include "roll/dice_choice.h"
#include "roll/dice_roll.h"

// The product (slice 0011): dice launched from rest by the controls or the serial port, rolled by
// the physics at 60 fps, and their results shown once every die locks.

namespace {

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t HALT_POLL_MS = 1000;
// Measured by the IMU probe (ADR-0005): shakes reach 9.8 g, so +-8 g clips them, and a shake
// is told from handling as well at 100 Hz as at the chips' fastest rates.
constexpr ImuSettings PRODUCT_IMU_SETTINGS{16, 2000, 100};

// Four 1/240 s steps make one 1/60 s frame (ADR-0007, ADR-0009). The physics follows frames, not
// the clock: a slow frame slows the dice rather than skipping steps the fairness test never ran.
constexpr uint8_t STEPS_PER_FRAME = 4;
// A roll that has not locked by now has a die the physics never brings to rest: the fairness
// test's limit, which no roll of its tens of thousands reached.
constexpr float ROLL_LIMIT_S = 20.0f;

constexpr int CANVAS_COLOR_DEPTH = 16;
// The tray fills the screen: 240 px across 105 mm is ADR-0007's 24 px D20 edge, and the 59 mm
// depth fills the 135 px height (src/physics/physics_config.h).
constexpr float PX_PER_MM = 240.0f / TRAY_WIDTH_MM;
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
        return _sprite.createSprite(tft.width(), tft.height()) != nullptr;
    }
    const char* failureReason() const override { return "no heap for the frame buffer"; }
    const char* description() const override { return "16-bit, full screen"; }

    LGFX_Sprite& sprite() { return _sprite; }

private:
    DisplayHAL& _display;
    LGFX_Sprite _sprite;
};

/** @brief What a roll's frames cost, kept as running figures: the StickC has no heap to spare for a buffer. */
struct FrameCosts {
    uint32_t frames;
    uint64_t workSumUs;
    uint32_t longestWorkUs;
    uint32_t longestPhysicsUs;
    uint32_t lateFrames;
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
uint32_t rollSteps = 0;
uint16_t rollNudges = 0;
bool rollReported = false;
FrameCosts costs{};

LGFX_Sprite* drawing = nullptr;
LGFX_Sprite* sending = nullptr;
uint32_t frameDeadlineUs = 0;

uint16_t packed(Rgb color) { return shadeRgb565(color, 1.0f); }

const DieShape& shapeOf(DieType type) { return type == DieType::D20 ? D20_SHAPE : D6_SHAPE; }

void launch() {
    world.shape = &shapeOf(choice.type);
    launchRoll(world, results, choice.count, randomness);
    rollSteps = 0;
    rollNudges = 0;
    rollReported = false;
    costs = {};
    Serial.printf("[roll] launch %s x%u\n", dieTypeName(choice.type), static_cast<unsigned>(choice.count));
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
    Serial.printf(" seconds=%.3f nudges=%u\n", seconds, static_cast<unsigned>(rollNudges));
    Serial.printf("[frame] frames=%lu work_mean_us=%lu work_max_us=%lu physics_max_us=%lu late=%lu\n",
                  static_cast<unsigned long>(costs.frames),
                  static_cast<unsigned long>(costs.frames == 0 ? 0 : costs.workSumUs / costs.frames),
                  static_cast<unsigned long>(costs.longestWorkUs), static_cast<unsigned long>(costs.longestPhysicsUs),
                  static_cast<unsigned long>(costs.lateFrames));
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
        // The screen's y runs down where the tray's runs up.
        const Vec3 centrePx{body.position.x * PX_PER_MM, canvas.height() - body.position.y * PX_PER_MM,
                            body.position.z * PX_PER_MM};
        viewDie(*world.shape, PX_PER_MM, toRotation(body.orientation), centrePx, DIE_COLORS[die], LIGHTING,
                NUMERAL_MIN_FACING, view);
        drawDieFaces(canvas, view);
        drawDieNumerals(canvas, view, packed(NUMERAL_COLOR));
    }
}

void drawFrame(LGFX_Sprite& canvas) {
    canvas.fillScreen(packed(TRAY_COLOR));
    drawDice(canvas);
    if (!rollReported || !everyDieLocked(results, world.dieCount)) return;
    uint8_t numerals[MAX_DICE];
    for (uint8_t die = 0; die < world.dieCount; die++) numerals[die] = results[die].numeral;
    const BandColors colors{packed(BAND_COLOR), packed(BAND_TEXT_COLOR), packed(NUMERAL_COLOR),
                            {packed(DIE_COLORS[0]), packed(DIE_COLORS[1]), packed(DIE_COLORS[2])}};
    drawResultsBand(canvas, resultsBand(dieTypeName(choice.type), numerals, world.dieCount), colors);
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

[[noreturn]] void haltOnFailure(const BootOutcome& outcome) {
    Serial.printf("[boot] %s FAILED: %s\n", outcome.failedStep->subsystem, outcome.failureReason);
    if (displayUp) {
        lgfx::LGFX_Device& tft = hal.display.getDevice();
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(TEXT_LEFT_PX, TEXT_TOP_PX);
        tft.printf("%s FAILED\n\n%s", outcome.failedStep->subsystem, outcome.failureReason);
    }
    // Nothing later may run on hardware that is only partly up.
    for (;;) delay(HALT_POLL_MS);
}

} // namespace

void setup() {
    Serial.begin(SERIAL_BAUD);
    hal.imu.configure(PRODUCT_IMU_SETTINGS);

    // Power first: the StickC switches itself off unless its HOLD pin is latched, and
    // the latch cannot fail, so nothing is lost by bringing the display up second.
    const BootStep steps[] = {
        {"power", hal.power},   {"display", hal.display}, {"canvas", front},     {"canvas", back},
        {"input", hal.input},   {"imu", hal.imu},         {"rng", randomness},
    };
    const BootOutcome outcome = runBootSteps(steps, sizeof(steps) / sizeof(steps[0]), logStarted);
    if (outcome.failedStep != nullptr) haltOnFailure(outcome);

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

    const uint32_t physicsStartUs = micros();
    for (uint8_t step = 0; step < STEPS_PER_FRAME; step++) {
        rollNudges += stepRoll(world, results, randomness, PRODUCT_REST_THRESHOLDS);
        rollSteps++;
    }
    const uint32_t physicsUs = micros() - physicsStartUs;

    drawFrame(*drawing);
    lgfx::LGFX_Device& tft = hal.display.getDevice();
    tft.waitDMA();
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
    if (pace.waitUs > 0) delayMicroseconds(pace.waitUs);
    frameDeadlineUs = pace.nextDeadlineUs;
}

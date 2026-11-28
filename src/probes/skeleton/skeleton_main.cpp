#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "../../boot/boot_steps.h"
#include "../../debug/screen_capture.h"
#include "../../hal/hal.h"

// The firmware skeleton (slice 0003): bring every subsystem up or stop loudly, then
// stream raw IMU samples to serial and show them on screen. The product replaced it as
// src/main.cpp in slice 0011; it stays as the firmware scripts/skeleton-verify.mjs checks
// the boards' HAL against.

namespace {

constexpr uint32_t SERIAL_BAUD = 115200;
// 50 Hz is a quarter of the StickC's 115200-baud bridge at about 50 bytes a line.
constexpr uint32_t SAMPLE_PERIOD_US = 20000;
// The readout is for eyes; a faster redraw would only take time from sampling.
constexpr uint32_t REDRAW_PERIOD_MS = 100;
constexpr uint32_t HALT_POLL_MS = 1000;
// Measured by the IMU probe (ADR-0005): shakes reach 9.8 g, so +-8 g clips them, and a shake
// is told from handling as well at 100 Hz as at the chips' fastest rates.
constexpr ImuSettings PRODUCT_IMU_SETTINGS{16, 2000, 100};

constexpr int TEXT_LEFT_PX = 6;
constexpr int TEXT_TOP_PX = 8;
constexpr int TEXT_LINE_PX = 14;
constexpr int GRAVITY_RADIUS_PX = 50;
constexpr int GRAVITY_RIGHT_MARGIN_PX = 8;
constexpr int GRAVITY_DOT_RADIUS_PX = 5;

/**
 * @brief The off-screen frame every redraw composes in, allocated once at boot.
 *
 * Constructed at file scope, so the constructor must not touch the display: the board's
 * display object lives in another file and may not be constructed yet. The sprite gets
 * no parent and is pushed to the device explicitly.
 */
class FrameCanvas : public Startable {
public:
    explicit FrameCanvas(DisplayHAL& display) : _display(display) {}

    bool begin() override {
        lgfx::LGFX_Device& tft = _display.getDevice();
        _sprite.setColorDepth(16);
        return _sprite.createSprite(tft.width(), tft.height()) != nullptr;
    }
    const char* failureReason() const override { return "no heap for the frame buffer"; }
    const char* description() const override { return "16-bit, full screen"; }

    LGFX_Sprite& sprite() { return _sprite; }
    void push() { _sprite.pushSprite(&_display.getDevice(), 0, 0); }

private:
    DisplayHAL& _display;
    LGFX_Sprite _sprite;
};

HardwareHAL& hal = getHAL();
FrameCanvas canvas(hal.display);
bool displayUp = false;

ImuSample latestSample{};
bool haveSample = false;
uint32_t failedReads = 0;
uint32_t nextSampleUs = 0;
uint32_t lastRedrawMs = 0;

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

const char* actionName(InputAction action) {
    switch (action) {
        case InputAction::PRIMARY: return "PRIMARY";
        case InputAction::SECONDARY: return "SECONDARY";
        case InputAction::UP: return "UP";
        case InputAction::DOWN: return "DOWN";
        case InputAction::LONG_PRIMARY: return "LONG_PRIMARY";
        case InputAction::LONG_SECONDARY: return "LONG_SECONDARY";
        case InputAction::NONE: break;
    }
    return nullptr;
}

void pollInput() {
    hal.input.update();
    // Characters and actions share one ordered queue (slice 0037). This probe reports actions, but
    // it reads the characters too and says what they were: a queue nobody drains fills up and the
    // driver then reports dropped input, which would read as a defect in a probe whose whole job
    // is to show what a key does.
    for (char typed = hal.input.getTypedCharacter(); typed != NO_CHARACTER; typed = hal.input.getTypedCharacter()) {
        Serial.printf("[input] typed %c\n", typed);
    }
    const char* name = actionName(hal.input.getAction());
    if (name != nullptr) Serial.printf("[input] %s\n", name);
}

void sampleImu() {
    ImuSample sample;
    if (!hal.imu.read(sample)) {
        failedReads++;
        Serial.printf("[imu] read FAILED (%lu failed so far)\n", static_cast<unsigned long>(failedReads));
        return;
    }
    latestSample = sample;
    haveSample = true;
    Serial.printf("[imu] %lu %.3f %.3f %.3f %.1f %.1f %.1f\n",
                  static_cast<unsigned long>(sample.timestampUs),
                  sample.accelG.x, sample.accelG.y, sample.accelG.z,
                  sample.gyroDps.x, sample.gyroDps.y, sample.gyroDps.z);
}

void drawReadout() {
    LGFX_Sprite& frame = canvas.sprite();
    frame.fillScreen(TFT_BLACK);
    frame.setTextColor(TFT_WHITE, TFT_BLACK);

    int y = TEXT_TOP_PX;
    frame.drawString(hal.boardName, TEXT_LEFT_PX, y);
    y += TEXT_LINE_PX * 3 / 2;
    if (!haveSample) {
        frame.drawString("waiting for IMU", TEXT_LEFT_PX, y);
        canvas.push();
        return;
    }

    const Vec3& accel = latestSample.accelG;
    const Vec3& gyro = latestSample.gyroDps;
    frame.setTextColor(TFT_CYAN, TFT_BLACK);
    frame.drawString("accel g", TEXT_LEFT_PX, y);
    y += TEXT_LINE_PX;
    const float accelAxes[3] = {accel.x, accel.y, accel.z};
    const float gyroAxes[3] = {gyro.x, gyro.y, gyro.z};
    const char axisNames[3] = {'x', 'y', 'z'};
    for (int axis = 0; axis < 3; axis++) {
        frame.setCursor(TEXT_LEFT_PX, y);
        frame.printf("%c %+6.2f", axisNames[axis], accelAxes[axis]);
        y += TEXT_LINE_PX;
    }
    frame.setTextColor(TFT_YELLOW, TFT_BLACK);
    frame.drawString("gyro dps", TEXT_LEFT_PX, y);
    y += TEXT_LINE_PX;
    for (int axis = 0; axis < 3; axis++) {
        frame.setCursor(TEXT_LEFT_PX, y);
        frame.printf("%c %+7.1f", axisNames[axis], gyroAxes[axis]);
        y += TEXT_LINE_PX;
    }

    // Gravity in the screen plane: the dot falls toward whichever edge is down, so a
    // wrong axis map shows before any scripted check runs. Screen y grows downward.
    const int centreX = frame.width() - GRAVITY_RIGHT_MARGIN_PX - GRAVITY_RADIUS_PX;
    const int centreY = frame.height() / 2;
    frame.drawCircle(centreX, centreY, GRAVITY_RADIUS_PX, TFT_DARKGREY);
    float planeX = -accel.x;
    float planeY = -accel.y;
    const float planeLength = sqrtf(planeX * planeX + planeY * planeY);
    if (planeLength > 1.0f) {
        planeX /= planeLength;
        planeY /= planeLength;
    }
    frame.fillCircle(centreX + static_cast<int>(planeX * GRAVITY_RADIUS_PX),
                     centreY - static_cast<int>(planeY * GRAVITY_RADIUS_PX),
                     GRAVITY_DOT_RADIUS_PX, TFT_GREEN);
    canvas.push();
}

} // namespace

void setup() {
    Serial.begin(SERIAL_BAUD);
    hal.imu.configure(PRODUCT_IMU_SETTINGS);

    // Power first: the StickC switches itself off unless its HOLD pin is latched, and
    // the latch cannot fail, so nothing is lost by bringing the display up second.
    const BootStep steps[] = {
        {"power", hal.power},
        {"display", hal.display},
        {"canvas", canvas},
        {"input", hal.input},
        {"imu", hal.imu},
    };
    const BootOutcome outcome = runBootSteps(steps, sizeof(steps) / sizeof(steps[0]), logStarted);
    if (outcome.failedStep != nullptr) haltOnFailure(outcome);

    Serial.printf("[boot] %s ready\n", hal.boardName);
    nextSampleUs = micros();
}

void loop() {
    // Before anything draws, so the canvas still holds the last whole frame pushed.
    serviceScreenCapture(canvas.sprite(), Serial);
    pollInput();

    const uint32_t nowUs = micros();
    if (static_cast<int32_t>(nowUs - nextSampleUs) >= 0) {
        sampleImu();
        nextSampleUs += SAMPLE_PERIOD_US;
        // After a stall (a screen capture takes seconds on the StickC), resume the
        // cadence instead of bursting the missed samples.
        if (static_cast<int32_t>(nowUs - nextSampleUs) >= 0) nextSampleUs = nowUs + SAMPLE_PERIOD_US;
    }

    const uint32_t nowMs = millis();
    if (nowMs - lastRedrawMs >= REDRAW_PERIOD_MS) {
        lastRedrawMs = nowMs;
        drawReadout();
    }
}

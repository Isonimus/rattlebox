#include "shaken_roll.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../imu/shake_detector.h"
#include "../../physics/tray_gravity.h"

namespace {

constexpr double US_PER_S = 1e6;
// The probe's CSV puts the timestamp and the three accelerometer axes first.
constexpr int LEADING_FIELDS = 4;
constexpr size_t LINE_CAPACITY = 256;

[[noreturn]] void malformed(const char* path, unsigned long line, const char* why) {
    fprintf(stderr, "%s:%lu: %s\n", path, line, why);
    exit(EXIT_FAILURE);
}

/** @brief How far the deepest vertex of @p world is past a plane of @p tray; negative inside. */
float deepestOutside(const World& world, const Tray& tray) {
    float deepest = -tray.heightMm;
    for (uint8_t die = 0; die < world.dieCount; die++) {
        for (uint8_t vertex = 0; vertex < world.shape->vertexCount; vertex++) {
            const Vec3 at = worldVertex(world.dice[die], *world.shape, vertex);
            const float outside[] = {-at.x, at.x - tray.widthMm, -at.y, at.y - tray.depthMm, -at.z, at.z - tray.heightMm};
            for (float past : outside) deepest = fmaxf(deepest, past);
        }
    }
    return deepest;
}

/**
 * @brief The recording's samples, then the device held still at its level angle. The hold starts
 * extraS after the first detection, or when the recording runs out.
 */
class ReplayInput {
public:
    ReplayInput(const ShakeRecording& recording, float extraS)
        : _recording(recording), _extraUs(static_cast<uint32_t>(extraS * US_PER_S)),
          _heldStill(normalized(recording.levelAccelG)) {}

    uint32_t nextTimestampUs() const {
        if (_index < _recording.samples.size()) return _recording.samples[_index].timestampUs;
        return _lastUs + _recording.periodUs;
    }

    ImuSample take() {
        ImuSample sample{};
        if (_index < _recording.samples.size()) {
            sample = _recording.samples[_index++];
        } else {
            sample.timestampUs = _lastUs + _recording.periodUs;
            _held = true;
        }
        _lastUs = sample.timestampUs;
        if (_detected && sample.timestampUs - _detectionUs >= _extraUs) _held = true;
        if (_held) sample.accelG = _heldStill;
        return sample;
    }

    void detectedAt(uint32_t timestampUs) {
        if (_detected) return;
        _detected = true;
        _detectionUs = timestampUs;
    }

    bool held() const { return _held; }

private:
    const ShakeRecording& _recording;
    const uint32_t        _extraUs;
    const Vec3            _heldStill;
    size_t                _index = 0;
    uint32_t              _lastUs = 0;
    uint32_t              _detectionUs = 0;
    bool                  _detected = false;
    bool                  _held = false;
};

} // namespace

bool continuesRecording(uint32_t previousUs, uint32_t nextUs) {
    const uint32_t gapUs = nextUs - previousUs;
    // A longer gap would leave the detector judging a window with nothing in it.
    return gapUs > 0 && gapUs < SHAKE_WINDOW_US;
}

ShakeRecording loadRecording(const char* path) {
    FILE* file = fopen(path, "r");
    if (file == nullptr) malformed(path, 0, "cannot open");
    ShakeRecording recording{};
    char line[LINE_CAPACITY];
    unsigned long lineNumber = 1;
    if (fgets(line, sizeof line, file) == nullptr) malformed(path, lineNumber, "no header");
    Vec3 sum{0, 0, 0};
    while (fgets(line, sizeof line, file) != nullptr) {
        lineNumber++;
        unsigned long timestampUs = 0;
        ImuSample sample{};
        if (sscanf(line, "%lu,%f,%f,%f", &timestampUs, &sample.accelG.x, &sample.accelG.y, &sample.accelG.z) !=
            LEADING_FIELDS) {
            malformed(path, lineNumber, "expected t_us,ax,ay,az first");
        }
        sample.timestampUs = static_cast<uint32_t>(timestampUs);
        if (!recording.samples.empty() && !continuesRecording(recording.samples.back().timestampUs, sample.timestampUs)) {
            malformed(path, lineNumber, "timestamp does not advance by less than the detector's window");
        }
        recording.samples.push_back(sample);
        sum = sum + sample.accelG;
    }
    fclose(file);
    const size_t count = recording.samples.size();
    if (count < 2) malformed(path, lineNumber, "fewer than two samples");
    recording.levelAccelG = sum * (1.0f / count);
    recording.periodUs = (recording.samples.back().timestampUs - recording.samples.front().timestampUs) / (count - 1);
    return recording;
}

ShakenOutcome simulateShakenRoll(World& world, const ShakeRecording& recording, float extraS, KickPlan kick,
                                 UniformSource& source, const RestThresholds& thresholds, float timeLimitS) {
    const Quaternion level = levelFrom(recording.levelAccelG);
    const double stepUs = PHYSICS_STEP_S * US_PER_S;
    const uint32_t startUs = recording.samples.front().timestampUs;
    const uint32_t limitSteps = stepsIn(timeLimitS);

    ShakenOutcome outcome{};
    RollOutcome& roll = outcome.roll;
    roll.dieCount = world.dieCount;
    for (uint8_t die = 0; die < world.dieCount; die++) {
        outcome.startNumerals[die] = shownNumeral(world, die);
        // The previous roll's results: locked, so the dice can move before the shake is detected
        // without anything changing.
        roll.results[die] = {DieState::Locked, outcome.startNumerals[die], 0};
    }
    outcome.deepestOutsideMm = deepestOutside(world, PRODUCT_TRAY);

    ShakeDetector detector;
    ReplayInput input(recording, extraS);
    bool shaking = false;
    uint32_t detectionStep = 0;
    Reorientation turns[MAX_DICE] = {};
    for (uint32_t step = 0;; step++) {
        const uint32_t nowUs = startUs + static_cast<uint32_t>(llround(step * stepUs));
        // Each sample holds until the next is due, as the device's latest reading would.
        while (input.nextTimestampUs() - startUs <= nowUs - startUs) {
            const ImuSample sample = input.take();
            detector.add(sample);
            world.gravity = trayGravity(level, sample.accelG);
            const bool wasShaking = shaking;
            shaking = detector.shaking();
            if (!shaking || wasShaking) continue;
            if (outcome.detections == 0) detectionStep = step;
            outcome.detections++;
            input.detectedAt(sample.timestampUs);
            for (uint8_t die = 0; die < world.dieCount; die++) {
                roll.launchNumerals[die] = shownNumeral(world, die);
                roll.results[die] = startRoll();
                if (kick.kind == ShakeKick::Reorient) world.dice[die].orientation = uniformOrientation(source);
                if (kick.kind != ShakeKick::None) throwDie(world.dice[die], source);
                if (kick.kind == ShakeKick::Spin) turns[die] = startReorientation(world.dice[die], kick.spinS, source);
            }
        }

        for (uint8_t die = 0; die < world.dieCount; die++) continueReorientation(turns[die], world.dice[die]);
        stepWorld(world, PRODUCT_TRAY);
        outcome.deepestOutsideMm = fmaxf(outcome.deepestOutsideMm, deepestOutside(world, PRODUCT_TRAY));
        for (uint8_t die = 0; die < world.dieCount; die++) {
            outcome.fastestMmPerS = fmaxf(outcome.fastestMmPerS, length(world.dice[die].velocity));
        }
        // A roll's dice do not lock while the shake goes on: a die resting a moment in the middle
        // of one is not a result.
        if (!shaking) roll.nudges += advanceResults(world, roll.results, source, thresholds);

        if (outcome.detections == 0) {
            if (input.held()) return outcome;
            continue;
        }
        roll.seconds = (step - detectionStep) * PHYSICS_STEP_S;
        if (input.held() && !shaking && everyDieLocked(roll.results, world.dieCount)) {
            roll.settled = true;
            for (uint8_t die = 0; die < world.dieCount; die++) roll.shownNumerals[die] = shownNumeral(world, die);
            return outcome;
        }
        if (step - detectionStep >= limitSteps) return outcome;
    }
}

void addShakenRoll(ShakenTally& tally, const ShakenOutcome& outcome) {
    tally.rolls++;
    tally.deepestOutsideMm = fmaxf(tally.deepestOutsideMm, outcome.deepestOutsideMm);
    tally.fastestMmPerS = fmaxf(tally.fastestMmPerS, outcome.fastestMmPerS);
    if (outcome.detections == 0) {
        tally.undetected++;
        return;
    }
    if (outcome.detections > 1) tally.redetected++;
    const RollOutcome& roll = outcome.roll;
    tally.nudges += roll.nudges;
    tally.totalS += roll.seconds;
    tally.longestS = fmaxf(tally.longestS, roll.seconds);
    if (!roll.settled) {
        tally.unsettled++;
        return;
    }
    for (uint8_t die = 0; die < roll.dieCount; die++) {
        const uint8_t numeral = roll.results[die].numeral;
        tally.counts[numeral]++;
        tally.results++;
        if (numeral == outcome.startNumerals[die]) tally.sameAsBefore++;
        if (numeral != roll.shownNumerals[die]) tally.knockedOver++;
    }
}

bool restingWorld(World& world, const DieShape& shape, uint8_t dice, UniformSource& source,
                  const RestThresholds& thresholds, float timeLimitS) {
    world = World{};
    world.shape = &shape;
    return simulateRollIn(world, dice, source, thresholds, timeLimitS, nullptr).settled;
}

float durationS(const ShakeRecording& recording) {
    return (recording.samples.back().timestampUs - recording.samples.front().timestampUs) / static_cast<float>(US_PER_S);
}

ShakenTally chainRolls(World& world, const std::vector<ShakeRecording>& recordings, unsigned long rolls, KickPlan kick,
                       UniformSource& source, const RestThresholds& thresholds, float timeLimitS) {
    ShakenTally tally{};
    for (unsigned long roll = 0; roll < rolls; roll++) {
        // One draw per statement, so the same seed picks the same way on every compiler.
        const size_t pick = static_cast<size_t>(source.nextUnit() * recordings.size());
        const ShakeRecording& recording = recordings[pick];
        const float extraS = source.nextUnit() * durationS(recording);
        addShakenRoll(tally, simulateShakenRoll(world, recording, extraS, kick, source, thresholds, timeLimitS));
    }
    return tally;
}

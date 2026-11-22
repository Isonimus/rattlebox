// Level probe (slice 0015, ADR-0014): why dice slid to the StickC's top edge when it was held
// upright like a phone and shaken, and whether the product's level rule stops it.
//
// Each take is replayed sample by sample through ShakeRoll as the device feeds it: the hold before
// the shake for the detector's window, the take, then the take's last reading held until every die
// locks. Each roll runs once per variant, on the same start and seed, so only the variant differs:
//   product       the level rule the device runs (ADR-0014): in flight, the reading's 0.1 s average;
//   frozen        slice 0012's rule: a roll in flight keeps the level it started with;
//   swapped-hold  frozen, with the hold before the shake read from the moment the hand stops: a
//                 control, under which no hold after the shake can differ from the level, so no
//                 level rule can do better.
// The frozen variants overwrite the gravity ShakeRoll sets with the one the frozen level gives.
//
// Takes from the gesture capture (hold upright, shake, hold to read) open with the hold before.
// The shake fixtures start mid-shake, so with --mean-hold their mean stands for both holds, as the
// fairness replay does.
//
// Usage: pio run -e native-level-probe &&
//        .pio/build/native-level-probe/program [--mean-hold] <take.csv>...

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "../../app/frame_pacer.h"
#include "../../geometry/die_shape.h"
#include "../../imu/shake_detector.h"
#include "../../imu/still_span.h"
#include "../../physics/tray_gravity.h"
#include "../../roll/dice_roll.h"
#include "../../roll/shake_roll.h"
#include "../fairness/seeded_source.h"
#include "../fairness/shaken_roll.h"

namespace {

constexpr uint8_t DICE = MAX_DICE;
constexpr unsigned ROLLS_PER_TAKE = 20;
constexpr uint64_t SEED = 1;
// As the fairness probe's tallies: far past the slowest roll to lock.
constexpr float TIME_LIMIT_S = 20.0f;
// A die whose centre is within this share of the tray from a wall lies against it: a D20 resting
// on a wall has its centre about a fifth of the way in.
constexpr float AGAINST_WALL_SHARE = 0.25f;
// The hand has stopped once the reading stays within this of 1 g: a shake swings it by several g,
// and a hand moving to the reading pose by a few tenths.
constexpr float HAND_STOPPED_G = 0.3f;

enum Variant { Product, Frozen, SwappedHold, VARIANT_COUNT };
const char* const VARIANT_NAMES[VARIANT_COUNT] = {"product", "frozen", "swapped-hold"};

struct Take {
    ShakeRecording recording;
    Vec3           beforeHoldG;
    Vec3           afterHoldG;
    /** @brief The first sample from which the hand has stopped for the rest of the take. */
    size_t         handStopped;
};

/**
 * @brief The mean of the take's opening samples that stay within STILL_SPREAD_G of its first: the
 * hold before the shake. The takes start shaking about 0.1 s in, before a still span could form.
 */
Vec3 firstHold(const ShakeRecording& recording) {
    const Vec3 first = recording.samples.front().accelG;
    Vec3 sum{0, 0, 0};
    unsigned count = 0;
    for (const ImuSample& sample : recording.samples) {
        if (length(sample.accelG - first) > STILL_SPREAD_G) break;
        sum = sum + sample.accelG;
        count++;
    }
    return sum * (1.0f / count);
}

size_t handStopped(const ShakeRecording& recording) {
    size_t stopped = recording.samples.size();
    while (stopped > 0 && fabsf(length(recording.samples[stopped - 1].accelG) - 1) < HAND_STOPPED_G) stopped--;
    return stopped;
}

struct RollEnd {
    bool  detected;
    bool  settled;
    float secondsToLock;
    /** @brief Each die's centre across the tray, 0 at one wall and 1 at the other: x to the screen's right, y to its top. */
    float widthShare[MAX_DICE];
    float depthShare[MAX_DICE];
};

RollEnd replay(World& world, const Take& take, Variant variant, UniformSource& source) {
    const ShakeRecording& recording = take.recording;
    ShakeRoll shakeRoll(PRODUCT_KICK);
    DieResult results[MAX_DICE];
    for (uint8_t die = 0; die < world.dieCount; die++) results[die] = {DieState::Locked, shownNumeral(world, die), 0};

    // The device was held at the hold before for the detector's window before the take began, as
    // on the device, where the still span and the level come into a shake from that hold.
    std::vector<ImuSample> samples;
    const uint32_t firstUs = recording.samples.front().timestampUs;
    const size_t heldBefore = SHAKE_WINDOW_US / recording.periodUs;
    for (size_t before = heldBefore; before > 0; before--) {
        ImuSample held{};
        held.timestampUs = firstUs - before * recording.periodUs;
        held.accelG = take.beforeHoldG;
        samples.push_back(held);
    }
    samples.insert(samples.end(), recording.samples.begin(), recording.samples.end());

    RollEnd end{};
    const double stepUs = PHYSICS_STEP_S * US_PER_S;
    const uint32_t startUs = samples.front().timestampUs;
    size_t next = 0;
    Quaternion frozenLevel = IDENTITY_QUATERNION;
    bool shakeEnded = false;
    uint32_t detectionStep = 0;
    for (uint32_t step = 0;; step++) {
        const uint32_t nowUs = startUs + static_cast<uint32_t>(llround(step * stepUs));
        // Each sample holds until the next is due, as the device's latest reading would; past the
        // take's end, the device is held at its last hold, sampled at the take's own period.
        for (;;) {
            ImuSample sample{};
            if (next < samples.size()) {
                sample = samples[next];
            } else {
                sample.timestampUs = startUs + static_cast<uint32_t>(next * recording.periodUs);
                sample.accelG = take.afterHoldG;
            }
            if (sample.timestampUs - startUs > nowUs - startUs) break;
            if (variant == SwappedHold && next >= heldBefore + take.handStopped) sample.accelG = take.beforeHoldG;
            next++;
            const bool wasInFlight = !everyDieLocked(results, world.dieCount);
            if (shakeRoll.addSample(sample, world, results, source)) {
                if (!end.detected) detectionStep = step;
                end.detected = true;
            }
            // The level in force when the roll took off: locked dice follow the still span under
            // either rule, so the product's level up to then is the frozen rule's too.
            if (!wasInFlight) frozenLevel = shakeRoll.level();
            if (variant != Product && shakeRoll.hasLevel()) world.gravity = trayGravity(frozenLevel, sample.accelG);
            if (end.detected && !shakeRoll.shaking()) shakeEnded = true;
        }
        shakeRoll.step(world, results, source, PRODUCT_REST_THRESHOLDS);
        if (!end.detected) {
            if (next >= samples.size()) return end;
            continue;
        }
        end.secondsToLock = (step - detectionStep) * PHYSICS_STEP_S;
        if (shakeEnded && !shakeRoll.shaking() && everyDieLocked(results, world.dieCount)) break;
        if (step - detectionStep >= stepsIn(TIME_LIMIT_S)) return end;
    }
    end.settled = true;
    for (uint8_t die = 0; die < world.dieCount; die++) {
        end.widthShare[die] = world.dice[die].position.x / PRODUCT_TRAY.widthMm;
        end.depthShare[die] = world.dice[die].position.y / PRODUCT_TRAY.depthMm;
    }
    return end;
}

struct EndTally {
    unsigned rolls = 0;
    unsigned unsettled = 0;
    unsigned dice = 0;
    unsigned atTop = 0;
    unsigned atBottom = 0;
    unsigned atSide = 0;
    double   depthShareSum = 0;
    double   secondsSum = 0;
};

void add(EndTally& tally, const RollEnd& end, uint8_t dieCount) {
    tally.rolls++;
    if (!end.settled) {
        tally.unsettled++;
        return;
    }
    tally.secondsSum += end.secondsToLock;
    for (uint8_t die = 0; die < dieCount; die++) {
        tally.dice++;
        tally.depthShareSum += end.depthShare[die];
        if (end.depthShare[die] > 1 - AGAINST_WALL_SHARE) tally.atTop++;
        if (end.depthShare[die] < AGAINST_WALL_SHARE) tally.atBottom++;
        if (fabsf(end.widthShare[die] - 0.5f) > 0.5f - AGAINST_WALL_SHARE) tally.atSide++;
    }
}

void print(const char* die, Variant variant, const EndTally& tally) {
    const unsigned settled = tally.rolls - tally.unsettled;
    const double dice = tally.dice > 0 ? tally.dice : 1;
    printf("die=%s variant=%s rolls=%u unsettled=%u mean_depth_share=%.2f at_top=%.2f at_bottom=%.2f "
           "at_side=%.2f mean_seconds_to_lock=%.1f\n",
           die, VARIANT_NAMES[variant], tally.rolls, tally.unsettled, tally.depthShareSum / dice, tally.atTop / dice,
           tally.atBottom / dice, tally.atSide / dice, settled > 0 ? tally.secondsSum / settled : 0.0);
}

void runDie(const char* name, const DieShape& shape, const std::vector<Take>& takes) {
    EndTally tallies[VARIANT_COUNT];
    SeededSource source(SEED);
    for (const Take& take : takes) {
        for (unsigned roll = 0; roll < ROLLS_PER_TAKE; roll++) {
            World start{};
            if (!restingWorld(start, shape, DICE, source, PRODUCT_REST_THRESHOLDS, TIME_LIMIT_S)) {
                fprintf(stderr, "a %s start never came to rest\n", name);
                exit(EXIT_FAILURE);
            }
            // The same start and the same draws for every variant, so only the variant differs.
            const uint64_t rollSeed = static_cast<uint64_t>(source.nextUnit() * UINT32_MAX) + 1;
            for (int variant = 0; variant < VARIANT_COUNT; variant++) {
                World world = start;
                SeededSource rollSource(rollSeed);
                const RollEnd end = replay(world, take, static_cast<Variant>(variant), rollSource);
                if (!end.detected) {
                    fprintf(stderr, "a %s replay was never detected under %s\n", name, VARIANT_NAMES[variant]);
                    exit(EXIT_FAILURE);
                }
                add(tallies[variant], end, DICE);
            }
        }
    }
    for (int variant = 0; variant < VARIANT_COUNT; variant++) print(name, static_cast<Variant>(variant), tallies[variant]);
}

} // namespace

int main(int argc, char** argv) {
    int first = 1;
    const bool meanHold = argc > 1 && strcmp(argv[1], "--mean-hold") == 0;
    if (meanHold) first++;
    if (argc <= first) {
        fprintf(stderr, "usage: %s [--mean-hold] <take.csv>...\n", argv[0]);
        return EXIT_FAILURE;
    }
    std::vector<Take> takes;
    for (int i = first; i < argc; i++) {
        Take take{loadRecording(argv[i]), {}, {}, 0};
        const ShakeRecording& recording = take.recording;
        take.beforeHoldG = meanHold ? normalized(recording.levelAccelG) : firstHold(recording);
        take.afterHoldG = meanHold ? take.beforeHoldG : recording.samples.back().accelG;
        take.handStopped = handStopped(recording);
        // A take that ends mid-shake never stops; the swapped hold then changes nothing in it.
        if (take.handStopped < recording.samples.size()) {
            printf("take=%s hand_stopped_s=%.2f\n", argv[i],
                   (recording.samples[take.handStopped].timestampUs - recording.samples.front().timestampUs) /
                       static_cast<double>(US_PER_S));
        }
        takes.push_back(take);
    }
    runDie("D6", makeD6(D6_EDGE_MM), takes);
    runDie("D20", makeD20(D20_EDGE_MM), takes);
    return 0;
}

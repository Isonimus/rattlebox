// Clack probe (slice 0013): the hits the shake fixtures make through the product's physics, kick
// and HitFinder, to choose the loudest-to-silent mapping of the clacks and to check that a shake
// rattles by its collisions alone.
//
// Each take is replayed whole on three dice resting where a left-alone roll leaves them, then the
// dice are left at rest a while longer. Every hit's impulse is kept by phase: while the detector
// reads a shake, after it until every die locks, and at rest after that, where any hit is one the
// clacks must not play.
//
// Usage: pio run -e native-clack-probe && .pio/build/native-clack-probe/program <take.csv>...

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "../../geometry/die_shape.h"
#include "../../roll/shake_roll.h"
#include "../../sound/hit_finder.h"
#include "../fairness/seeded_source.h"
#include "../fairness/shaken_roll.h"

namespace {

constexpr uint8_t DICE = MAX_DICE;
constexpr unsigned ROLLS_PER_TAKE = 20;
constexpr uint64_t SEED = 1;
// As the fairness probe's tallies: far past the slowest roll to lock.
constexpr float TIME_LIMIT_S = 20.0f;
// Long past the longest pause after which a resting die still tipped (0.037 s, slice 0008).
constexpr float REST_AFTER_S = 2.0f;
// One die's weight held for a step: what a resting contact pushes with, the scale to read the
// thresholds against.
constexpr float RESTING_IMPULSE = DIE_MASS * GRAVITY_MM_PER_S2 * PHYSICS_STEP_S;
constexpr float THRESHOLDS[] = {0, 25, 50, 100, 200, 400, 800, 1600};
// About the gap a listener hears as a break in a rattle rather than its texture.
constexpr float SILENCE_WINDOW_S = 0.05f;
constexpr uint32_t STEPS_PER_WINDOW = static_cast<uint32_t>(SILENCE_WINDOW_S / PHYSICS_STEP_S + 0.5f);
constexpr float PERCENTILES[] = {0.5f, 0.9f, 0.99f, 1.0f};

enum Phase { Shaking, Settling, Resting, PHASE_COUNT };
const char* const PHASE_NAMES[PHASE_COUNT] = {"shaking", "settling", "resting"};

struct PhaseTally {
    std::vector<float> impulses;
    uint32_t steps = 0;
    // For Shaking only: SILENCE_WINDOW_S windows, and those with a hit of at least each threshold.
    uint32_t windows = 0;
    uint32_t windowsWithHit[sizeof THRESHOLDS / sizeof THRESHOLDS[0]] = {};
};

/** @brief Each step's hits, by phase, with the windows of the shake that held one. */
class HitTally : public ReplayObserver {
public:
    explicit HitTally(PhaseTally* phases) : _phases(phases) {}

    void stepped(const World& world, bool shaking) override {
        if (shaking) _shaken = true;
        // Before the first detection, the dice still rest from the start: not a phase of the roll.
        if (!_shaken) return;
        record(world, shaking ? Shaking : Settling);
    }

    void record(const World& world, Phase phase) {
        float impulses[MAX_HITS_PER_STEP];
        const uint8_t count = _finder.find(world, impulses);
        PhaseTally& tally = _phases[phase];
        tally.steps++;
        float loudest = 0;
        for (uint8_t i = 0; i < count; i++) {
            tally.impulses.push_back(impulses[i]);
            loudest = fmaxf(loudest, impulses[i]);
        }
        if (phase != Shaking) return;
        _windowLoudest = fmaxf(_windowLoudest, loudest);
        if (++_windowSteps < STEPS_PER_WINDOW) return;
        tally.windows++;
        for (size_t i = 0; i < sizeof THRESHOLDS / sizeof THRESHOLDS[0]; i++) {
            if (_windowLoudest > THRESHOLDS[i]) tally.windowsWithHit[i]++;
        }
        _windowSteps = 0;
        _windowLoudest = 0;
    }

private:
    PhaseTally* _phases;
    HitFinder   _finder;
    bool        _shaken = false;
    uint32_t    _windowSteps = 0;
    float       _windowLoudest = 0;
};

void printPhase(const char* die, Phase phase, PhaseTally& tally) {
    const double seconds = tally.steps * PHYSICS_STEP_S;
    printf("die=%s phase=%s seconds=%.1f hits=%zu", die, PHASE_NAMES[phase], seconds, tally.impulses.size());
    for (float threshold : THRESHOLDS) {
        const size_t above = std::count_if(tally.impulses.begin(), tally.impulses.end(),
                                           [threshold](float impulse) { return impulse > threshold; });
        printf(" per_s_over_%.0f=%.2f", threshold, seconds > 0 ? above / seconds : 0.0);
    }
    std::sort(tally.impulses.begin(), tally.impulses.end());
    for (float share : PERCENTILES) {
        const float value = tally.impulses.empty()
                                ? 0
                                : tally.impulses[static_cast<size_t>(share * (tally.impulses.size() - 1))];
        printf(" p%.0f=%.1f", share * 100, value);
    }
    if (phase == Shaking) {
        for (size_t i = 0; i < sizeof THRESHOLDS / sizeof THRESHOLDS[0]; i++) {
            printf(" windows_with_hit_over_%.0f=%.3f", THRESHOLDS[i],
                   tally.windows > 0 ? static_cast<double>(tally.windowsWithHit[i]) / tally.windows : 0.0);
        }
    }
    printf("\n");
}

void runDie(const char* name, const DieShape& shape, const std::vector<ShakeRecording>& recordings) {
    PhaseTally phases[PHASE_COUNT];
    SeededSource source(SEED);
    unsigned unsettled = 0;
    for (const ShakeRecording& recording : recordings) {
        for (unsigned roll = 0; roll < ROLLS_PER_TAKE; roll++) {
            World world{};
            if (!restingWorld(world, shape, DICE, source, PRODUCT_REST_THRESHOLDS, TIME_LIMIT_S)) {
                fprintf(stderr, "a %s start never came to rest\n", name);
                exit(EXIT_FAILURE);
            }
            HitTally tally(phases);
            const ShakenOutcome outcome = simulateShakenRoll(world, recording, durationS(recording), PRODUCT_KICK, source,
                                                             PRODUCT_REST_THRESHOLDS, TIME_LIMIT_S, &tally);
            if (outcome.detections == 0) {
                fprintf(stderr, "a %s replay was never detected\n", name);
                exit(EXIT_FAILURE);
            }
            if (!outcome.roll.settled) unsettled++;
            for (uint32_t step = 0; step < stepsIn(REST_AFTER_S); step++) {
                stepWorld(world, PRODUCT_TRAY);
                tally.record(world, Resting);
            }
        }
    }
    printf("die=%s takes=%zu rolls=%zu unsettled=%u resting_impulse=%.1f\n", name, recordings.size(),
           recordings.size() * ROLLS_PER_TAKE, unsettled, RESTING_IMPULSE);
    for (int phase = 0; phase < PHASE_COUNT; phase++) printPhase(name, static_cast<Phase>(phase), phases[phase]);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <take.csv>...\n", argv[0]);
        return EXIT_FAILURE;
    }
    std::vector<ShakeRecording> recordings;
    for (int i = 1; i < argc; i++) recordings.push_back(loadRecording(argv[i]));
    runDie("D6", makeD6(D6_EDGE_MM), recordings);
    runDie("D20", makeD20(D20_EDGE_MM), recordings);
    return 0;
}

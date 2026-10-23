#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

#include "../../geometry/die_shape.h"
#include "../../physics/die_result.h"
#include "../../physics/physics_config.h"
#include "seeded_source.h"
#include "shaken_roll.h"
#include "simulated_roll.h"

// The fairness probe (slice 0008), a host program. scripts/fairness-probe.mjs runs it.
//
//   trace <D6|D20> <rolls> <seed>         one die per roll, left to move for TRACE_SECONDS with
//                                         nothing locking, to measure what the rest thresholds,
//                                         hold time and cocked tolerance are chosen from
//   tally <D6|D20> <dice> <rolls> <seed>  rolls through the product's thresholds, counting results
//   replay <D6|D20> <dice> <trials> <seed> <none|throw|reorient|spin=S> <extra_s|full> <csv>
//                                         dice at rest from a launch, then the IMU recording replayed
//                                         as the tray's gravity, cut extra_s after the shake is
//                                         detected: how often the result repeats, and whether the
//                                         physics holds up under a real shake
//   chain <D6|D20> <dice> <rolls> <seed> <none|throw|reorient|spin=S> <csv>...
//                                         each roll a random recording, cut at a random point, on the
//                                         dice where the last roll left them, counting results
//
// Each prints one line per roll (trace) or per run (tally, replay, chain) for the host to aggregate.

namespace {

constexpr int EXIT_USAGE = 2;
constexpr float RAD_TO_DEG = 57.2957795f;
// Long enough that every die has been still for seconds: thrown dice came to rest in under half
// a second in the design's experiments.
constexpr float TRACE_SECONDS = 6.0f;
constexpr uint32_t TRACE_STEPS = stepsIn(TRACE_SECONDS);
// The last second of a trace shows how still a die resting on a face gets: the floor the
// thresholds must sit above.
constexpr uint32_t FLOOR_STEPS = stepsIn(1.0f);
// A tally roll still moving after this long has a die the physics never brings to rest.
constexpr float TALLY_TIME_LIMIT_S = 20.0f;
// Nothing locks while tracing, so every step of the roll is recorded.
constexpr RestThresholds NEVER_LOCK{0, 0, TRACE_SECONDS * 2, 0};

// The candidate thresholds a trace measures against: from below the resting floor to well
// above a die rocking on an edge.
constexpr float CANDIDATE_LINEAR_MM_PER_S[] = {1, 2, 5, 10, 20};
constexpr float CANDIDATE_ANGULAR_RAD_PER_S[] = {0.1f, 0.2f, 0.5f, 1, 2};
constexpr size_t LINEAR_CANDIDATES = sizeof(CANDIDATE_LINEAR_MM_PER_S) / sizeof(CANDIDATE_LINEAR_MM_PER_S[0]);
constexpr size_t ANGULAR_CANDIDATES = sizeof(CANDIDATE_ANGULAR_RAD_PER_S) / sizeof(CANDIDATE_ANGULAR_RAD_PER_S[0]);

struct TraceStep {
    float   linearMmPerS;
    float   angularRadPerS;
    uint8_t upFace;
    float   tiltRad;
};

/** @brief Records the one die of a trace roll, every step. */
class TraceRecorder : public StepObserver {
public:
    explicit TraceRecorder(const DieShape& shape) : _shape(shape) {}

    void onStep(const World& world) override {
        if (_count >= TRACE_STEPS) abort();
        const DieBody& body = world.dice[0];
        const UpFace top = upFace(_shape, body.orientation);
        _steps[_count++] = {length(body.velocity), length(body.angularVelocity), top.face, top.tiltRad};
    }

    uint32_t count() const { return _count; }
    const TraceStep& at(uint32_t step) const { return _steps[step]; }

private:
    const DieShape& _shape;
    TraceStep       _steps[TRACE_STEPS];
    uint32_t        _count = 0;
};

/** @brief The last step at which the die's up face differed from the step before it. */
uint32_t lastFaceChange(const TraceRecorder& trace) {
    uint32_t last = 0;
    for (uint32_t step = 1; step < trace.count(); step++) {
        if (trace.at(step).upFace != trace.at(step - 1).upFace) last = step;
    }
    return last;
}

/**
 * @brief The longest stretch below both thresholds that began before the up face's last change:
 * a die that paused that long and then tipped onto another face. A hold time must outlast it,
 * or the die would lock to a face it then left.
 */
float longestFalseRestS(const TraceRecorder& trace, uint32_t lastChange, float linear, float angular) {
    uint32_t longest = 0;
    uint32_t run = 0;
    for (uint32_t step = 0; step < lastChange; step++) {
        const TraceStep& here = trace.at(step);
        run = (here.linearMmPerS < linear && here.angularRadPerS < angular) ? run + 1 : 0;
        if (run > longest) longest = run;
    }
    return longest * PHYSICS_STEP_S;
}

bool parseShape(const char* name, DieShape& shape) {
    if (strcmp(name, "D6") == 0) {
        shape = makeD6(D6_EDGE_MM);
        return true;
    }
    if (strcmp(name, "D20") == 0) {
        shape = makeD20(D20_EDGE_MM);
        return true;
    }
    return false;
}

bool parseCount(const char* text, unsigned long& value) {
    char* end = nullptr;
    value = strtoul(text, &end, 10);
    return end != text && *end == '\0' && value > 0;
}

bool parseSeconds(const char* text, float& value) {
    char* end = nullptr;
    value = strtof(text, &end);
    return end != text && *end == '\0' && value >= 0;
}

/** @brief none, throw, reorient, or spin=<seconds>. */
bool parseKick(const char* name, KickPlan& kick) {
    static const char SPIN_PREFIX[] = "spin=";
    kick = {ShakeKick::None, 0};
    if (strcmp(name, "none") == 0) return true;
    if (strcmp(name, "throw") == 0) {
        kick.kind = ShakeKick::Throw;
        return true;
    }
    if (strcmp(name, "reorient") == 0) {
        kick.kind = ShakeKick::Reorient;
        return true;
    }
    if (strncmp(name, SPIN_PREFIX, sizeof SPIN_PREFIX - 1) == 0) {
        kick.kind = ShakeKick::Spin;
        return parseSeconds(name + sizeof SPIN_PREFIX - 1, kick.spinS) && kick.spinS > 0;
    }
    return false;
}

int usage() {
    fprintf(stderr, "usage: fairness_probe trace <D6|D20> <rolls> <seed>\n"
                    "       fairness_probe tally <D6|D20> <dice 1-%u> <rolls> <seed>\n"
                    "       fairness_probe replay <D6|D20> <dice> <trials> <seed> <none|throw|reorient|spin=S> <extra_s|full> <csv>\n"
                    "       fairness_probe chain <D6|D20> <dice> <rolls> <seed> <none|throw|reorient|spin=S> <csv>...\n",
            static_cast<unsigned>(MAX_DICE));
    return EXIT_USAGE;
}

double secondsSince(clock_t start) { return static_cast<double>(clock() - start) / CLOCKS_PER_SEC; }

int runTrace(const char* name, const DieShape& shape, unsigned long rolls, unsigned long seed) {
    printf("[trace-grid] linear=");
    for (size_t i = 0; i < LINEAR_CANDIDATES; i++) printf(i ? ",%g" : "%g", CANDIDATE_LINEAR_MM_PER_S[i]);
    printf(" angular=");
    for (size_t i = 0; i < ANGULAR_CANDIDATES; i++) printf(i ? ",%g" : "%g", CANDIDATE_ANGULAR_RAD_PER_S[i]);
    printf("\n");

    SeededSource source(seed);
    for (unsigned long roll = 0; roll < rolls; roll++) {
        TraceRecorder recorder(shape);
        const TraceRecorder* trace = &recorder;
        simulateRoll(shape, 1, source, NEVER_LOCK, TRACE_SECONDS, &recorder);
        float floorLinear = 0;
        float floorAngular = 0;
        for (uint32_t step = trace->count() - FLOOR_STEPS; step < trace->count(); step++) {
            floorLinear = fmaxf(floorLinear, trace->at(step).linearMmPerS);
            floorAngular = fmaxf(floorAngular, trace->at(step).angularRadPerS);
        }
        const uint32_t lastChange = lastFaceChange(*trace);
        printf("[trace] die=%s roll=%lu final_tilt_deg=%.4f last_face_change_s=%.4f floor_linear=%.6f "
               "floor_angular=%.6f false_rest_s=",
               name, roll, trace->at(trace->count() - 1).tiltRad * RAD_TO_DEG, lastChange * PHYSICS_STEP_S,
               floorLinear, floorAngular);
        bool first = true;
        for (float linear : CANDIDATE_LINEAR_MM_PER_S) {
            for (float angular : CANDIDATE_ANGULAR_RAD_PER_S) {
                printf(first ? "%.4f" : ",%.4f", longestFalseRestS(*trace, lastChange, linear, angular));
                first = false;
            }
        }
        printf("\n");
    }
    return 0;
}

int runTally(const char* name, const DieShape& shape, uint8_t dice, unsigned long rolls, unsigned long seed) {
    SeededSource source(seed);
    unsigned long counts[DIE_MAX_FACES + 1] = {};
    unsigned long nudges = 0;
    unsigned long mostNudges = 0;
    unsigned long unsettled = 0;
    unsigned long onLaunchFace = 0;
    unsigned long knockedOver = 0;
    double totalS = 0;
    float longestS = 0;
    const clock_t start = clock();
    for (unsigned long roll = 0; roll < rolls; roll++) {
        const RollOutcome outcome = simulateRoll(shape, dice, source, PRODUCT_REST_THRESHOLDS, TALLY_TIME_LIMIT_S, nullptr);
        if (!outcome.settled) unsettled++;
        nudges += outcome.nudges;
        if (outcome.nudges > mostNudges) mostNudges = outcome.nudges;
        totalS += outcome.seconds;
        longestS = fmaxf(longestS, outcome.seconds);
        for (uint8_t die = 0; die < dice; die++) {
            const DieResult& result = outcome.results[die];
            if (result.state != DieState::Locked) continue;
            counts[result.numeral]++;
            if (result.numeral == outcome.launchNumerals[die]) onLaunchFace++;
            if (result.numeral != outcome.shownNumerals[die]) knockedOver++;
        }
    }
    printf("[tally] die=%s dice=%u rolls=%lu seed=%lu counts=", name, static_cast<unsigned>(dice), rolls, seed);
    for (uint8_t numeral = 1; numeral <= shape.faceCount; numeral++) printf(numeral > 1 ? ",%lu" : "%lu", counts[numeral]);
    printf(" nudges=%lu most_nudges_in_a_roll=%lu unsettled=%lu on_launch_face=%lu knocked_over=%lu "
           "mean_roll_s=%.4f longest_roll_s=%.4f runtime_s=%.3f\n",
           nudges, mostNudges, unsettled, onLaunchFace, knockedOver, totalS / rolls, longestS, secondsSince(start));
    return 0;
}

/** @brief What replay and chain add up over their rolls, printed as one line. */
struct ShakenTally {
    unsigned long counts[DIE_MAX_FACES + 1];
    unsigned long results;
    unsigned long sameAsBefore;
    unsigned long undetected;
    unsigned long redetected;
    unsigned long unsettled;
    unsigned long nudges;
    unsigned long knockedOver;
    float         deepestOutsideMm;
    float         fastestMmPerS;
    double        totalS;
    float         longestS;
    unsigned long rolls;
};

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

void printShakenTally(const ShakenTally& tally, uint8_t faceCount, clock_t start) {
    printf("counts=");
    for (uint8_t numeral = 1; numeral <= faceCount; numeral++) printf(numeral > 1 ? ",%lu" : "%lu", tally.counts[numeral]);
    const unsigned long detected = tally.rolls - tally.undetected;
    printf(" rolls=%lu results=%lu same_as_before=%lu undetected=%lu redetected=%lu unsettled=%lu nudges=%lu "
           "knocked_over=%lu deepest_outside_mm=%.4f fastest_mm_per_s=%.1f mean_roll_s=%.4f longest_roll_s=%.4f "
           "runtime_s=%.3f\n",
           tally.rolls, tally.results, tally.sameAsBefore, tally.undetected, tally.redetected, tally.unsettled,
           tally.nudges, tally.knockedOver, tally.deepestOutsideMm, tally.fastestMmPerS,
           detected ? tally.totalS / detected : 0.0, tally.longestS, secondsSince(start));
}

/** @brief A World of @p dice dice of @p shape resting where a left-alone roll leaves them, or false if it never settled. */
bool restingWorld(World& world, const DieShape& shape, uint8_t dice, UniformSource& source) {
    world = World{};
    world.shape = &shape;
    return simulateRollIn(world, dice, source, PRODUCT_REST_THRESHOLDS, TALLY_TIME_LIMIT_S, nullptr).settled;
}

float durationS(const ShakeRecording& recording) {
    return (recording.samples.back().timestampUs - recording.samples.front().timestampUs) / 1e6f;
}

int runReplay(const DieShape& shape, uint8_t dice, unsigned long trials, unsigned long seed, KickPlan kick, const char* kickText,
              const char* extraText, const char* path) {
    const ShakeRecording recording = loadRecording(path);
    float extraS = durationS(recording);
    if (strcmp(extraText, "full") != 0 && !parseSeconds(extraText, extraS)) return usage();
    SeededSource source(seed);
    ShakenTally tally{};
    unsigned long unsettledStarts = 0;
    const clock_t start = clock();
    for (unsigned long trial = 0; trial < trials; trial++) {
        World world{};
        if (!restingWorld(world, shape, dice, source)) {
            unsettledStarts++;
            continue;
        }
        addShakenRoll(tally, simulateShakenRoll(world, recording, extraS, kick, source, PRODUCT_REST_THRESHOLDS,
                                                TALLY_TIME_LIMIT_S));
    }
    printf("[replay] file=%s dice=%u kick=%s extra_s=%.3f seed=%lu unsettled_starts=%lu ", path,
           static_cast<unsigned>(dice), kickText, extraS, seed, unsettledStarts);
    printShakenTally(tally, shape.faceCount, start);
    return 0;
}

int runChain(const DieShape& shape, uint8_t dice, unsigned long rolls, unsigned long seed, KickPlan kick, const char* kickText,
             char** paths, int pathCount) {
    std::vector<ShakeRecording> recordings;
    for (int i = 0; i < pathCount; i++) recordings.push_back(loadRecording(paths[i]));
    SeededSource source(seed);
    World world{};
    if (!restingWorld(world, shape, dice, source)) {
        fprintf(stderr, "the first roll never came to rest\n");
        return EXIT_FAILURE;
    }
    ShakenTally tally{};
    const clock_t start = clock();
    for (unsigned long roll = 0; roll < rolls; roll++) {
        // One draw per statement, so the same seed picks the same way on every compiler.
        const size_t pick = static_cast<size_t>(source.nextUnit() * recordings.size());
        const ShakeRecording& recording = recordings[pick];
        const float extraS = source.nextUnit() * durationS(recording);
        addShakenRoll(tally, simulateShakenRoll(world, recording, extraS, kick, source, PRODUCT_REST_THRESHOLDS,
                                                TALLY_TIME_LIMIT_S));
    }
    printf("[chain] dice=%u kick=%s seed=%lu recordings=%d ", static_cast<unsigned>(dice),
           kickText, seed, pathCount);
    printShakenTally(tally, shape.faceCount, start);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    DieShape shape{};
    unsigned long rolls = 0;
    unsigned long seed = 0;
    if (strcmp(argv[1], "trace") == 0 && argc == 5) {
        if (!parseShape(argv[2], shape) || !parseCount(argv[3], rolls) || !parseCount(argv[4], seed)) return usage();
        return runTrace(argv[2], shape, rolls, seed);
    }
    unsigned long dice = 0;
    if (strcmp(argv[1], "tally") == 0 && argc == 6) {
        if (!parseShape(argv[2], shape) || !parseCount(argv[3], dice) || dice > MAX_DICE ||
            !parseCount(argv[4], rolls) || !parseCount(argv[5], seed)) {
            return usage();
        }
        return runTally(argv[2], shape, static_cast<uint8_t>(dice), rolls, seed);
    }
    KickPlan kick{ShakeKick::None, 0};
    const bool shakenHead = argc >= 8 && parseShape(argv[2], shape) && parseCount(argv[3], dice) && dice <= MAX_DICE &&
                            parseCount(argv[4], rolls) && parseCount(argv[5], seed) && parseKick(argv[6], kick);
    if (strcmp(argv[1], "replay") == 0 && argc == 9 && shakenHead) {
        return runReplay(shape, static_cast<uint8_t>(dice), rolls, seed, kick, argv[6], argv[7], argv[8]);
    }
    if (strcmp(argv[1], "chain") == 0 && shakenHead) {
        return runChain(shape, static_cast<uint8_t>(dice), rolls, seed, kick, argv[6], argv + 7, argc - 7);
    }
    return usage();
}

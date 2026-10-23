#ifndef SHAKEN_ROLL_H
#define SHAKEN_ROLL_H

#include <stdint.h>
#include <vector>
#include "../../imu/imu_sample.h"
#include "../../physics/reorientation.h"
#include "simulated_roll.h"

/** @brief An IMU capture from the IMU probe (slice 0004), on the device frame, replayed as live input. */
struct ShakeRecording {
    std::vector<ImuSample> samples;
    /** @brief The mean reading: the angle the device was held at, taken as level for the replay. */
    Vec3 levelAccelG;
    /** @brief The mean time between samples, from their timestamps (invariant #4). */
    uint32_t periodUs;
};

/**
 * @brief Whether a sample at @p nextUs can follow one at @p previousUs in one continuous recording:
 * later, by less than the shake detector's window. The difference is unsigned, so a clock that
 * wraps passes and one that steps back reads as a gap of about 71 minutes and fails.
 */
bool continuesRecording(uint32_t previousUs, uint32_t nextUs);

/** @brief Read the probe's CSV (t_us,ax,ay,az,gx,gy,gz,...); exits with a message on any malformed line. */
ShakeRecording loadRecording(const char* path);

/** @brief What lands on the dice the moment a shake is detected, from the RNG (ADR-0002). */
enum class ShakeKick : uint8_t {
    None,
    /** @brief ADR-0009's throw added to each die's motion. */
    Throw,
    /** @brief The throw, and each die turned to a fresh uniform orientation: a visible jump. */
    Reorient,
    /** @brief The throw, and each die spun to a fresh uniform orientation over a set time. */
    Spin,
};

/** @brief A kick, with the time a Spin takes. */
struct KickPlan {
    ShakeKick kind;
    float     spinS;
};

struct ShakenOutcome {
    /** @brief Results, nudges and seconds as a left-alone roll reports them; seconds count from the detection. */
    RollOutcome roll;
    /** @brief The numeral each die showed before the shake: the previous roll's result. */
    uint8_t     startNumerals[MAX_DICE];
    /** @brief How many times the detector went from still to shaking; each starts the roll over. */
    uint8_t     detections;
    /** @brief The deepest any vertex went past a plane of the tray, over the whole replay. */
    float       deepestOutsideMm;
    float       fastestMmPerS;
};

/**
 * @brief Replay @p recording as the tray's gravity on dice resting in @p world, as the product
 * would: each detection starts a new roll and applies @p kick; nothing locks while the detector
 * reads a shake; @p extraS after the first detection the device is held still and level, and the
 * roll runs until every die locks or @p timeLimitS after the detection.
 *
 * @p world keeps the dice where they end, so the next roll can start from this one.
 */
ShakenOutcome simulateShakenRoll(World& world, const ShakeRecording& recording, float extraS, KickPlan kick,
                                 UniformSource& source, const RestThresholds& thresholds, float timeLimitS);

/** @brief Shaken rolls added up: the results, and what went wrong on the way to them. */
struct ShakenTally {
    unsigned long counts[DIE_MAX_FACES + 1];
    unsigned long results;
    /**
     * @brief Results on the face their die showed before the shake: 1 in faces from a fair roll.
     * The face shown, not the last locked result: the bias this counts is the die staying near
     * where it lies. The two differ only for a die knocked over after locking, 0.18% of D20
     * results at most and none with one die (ADR-0010).
     */
    unsigned long sameAsBefore;
    unsigned long undetected;
    unsigned long redetected;
    unsigned long unsettled;
    unsigned long nudges;
    /** @brief Locked results whose die was knocked onto another face before the roll ended. */
    unsigned long knockedOver;
    float         deepestOutsideMm;
    float         fastestMmPerS;
    double        totalS;
    float         longestS;
    unsigned long rolls;
};

void addShakenRoll(ShakenTally& tally, const ShakenOutcome& outcome);

/** @brief Set @p world to @p dice dice of @p shape resting where a left-alone roll leaves them; false if it never settled. */
bool restingWorld(World& world, const DieShape& shape, uint8_t dice, UniformSource& source,
                  const RestThresholds& thresholds, float timeLimitS);

float durationS(const ShakeRecording& recording);

/**
 * @brief Roll the dice in @p world @p rolls times one after another, as the device does: each roll
 * starts where the last one left the dice, on a random one of @p recordings cut at a random time
 * after its detection.
 */
ShakenTally chainRolls(World& world, const std::vector<ShakeRecording>& recordings, unsigned long rolls, KickPlan kick,
                       UniformSource& source, const RestThresholds& thresholds, float timeLimitS);

#endif // SHAKEN_ROLL_H

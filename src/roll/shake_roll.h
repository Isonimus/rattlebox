#ifndef SHAKE_ROLL_H
#define SHAKE_ROLL_H

#include <stdint.h>
#include "../geometry/quaternion.h"
#include "../imu/imu_sample.h"
#include "../imu/reading_average.h"
#include "../imu/shake_detector.h"
#include "../imu/still_span.h"
#include "../physics/die_result.h"
#include "../physics/launch.h"
#include "../physics/reorientation.h"
#include "../physics/world.h"

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

// ADR-0014: below this the averaged reading has no direction to level by, as in free fall. The
// smallest 0.1 s average over all 39 captures at 100 Hz was 0.165 g, in the hardest shake.
constexpr float LEVEL_MIN_AVERAGE_G = 0.1f;

/** @brief A kick, with the time a Spin takes. */
struct KickPlan {
    ShakeKick kind;
    float     spinS;
};

/**
 * @brief The product's kick (ADR-0010). The others exist for the fairness probe, and for the
 * fairness test's negative control, which shows the repeat check fails without the spin.
 */
constexpr KickPlan PRODUCT_KICK{ShakeKick::Spin, REORIENTATION_S};

/**
 * @brief Dice rolled by the device's own motion (ADR-0010, slice 0012): the accelerometer is the
 * tray's gravity, and each shake the detector picks out starts a new roll and kicks every die.
 *
 * The device, the fairness test and the fairness probe all roll through this class, so invariant
 * #2 tests the code the device runs.
 *
 * Level is the reading of the latest still span since the dice locked while every die is locked,
 * and the level the roll ended on until there is one (slice 0018). While a roll is in flight it
 * follows the reading averaged over 0.1 s (ADR-0014), so the angle a hand settles into to read the
 * result is level by the time the dice lie still. Until the first still span, the tray stays level.
 */
class ShakeRoll {
public:
    explicit ShakeRoll(KickPlan kick) : _kick(kick) {}

    /**
     * @brief Take one IMU sample: the tray's gravity follows it and, on the detector's rising edge,
     * every result starts over and every die is kicked. Returns whether this sample started a roll.
     */
    bool addSample(const ImuSample& sample, World& world, DieResult* results, UniformSource& source);

    /**
     * @brief One physics step: each die's turn, the world, then the results, which do not advance
     * while the detector reads a shake. Returns how many dice were nudged.
     */
    uint8_t step(World& world, DieResult* results, UniformSource& source, const RestThresholds& thresholds);

    /** @brief launchRoll, cancelling any turn in progress, which would otherwise override the launch's orientation. */
    void launch(World& world, DieResult* results, uint8_t dieCount, UniformSource& source);

    bool shaking() const { return _shaking; }
    bool hasLevel() const { return _hasLevel; }
    /** @brief The turn that makes the holding angle level; the identity until hasLevel(). */
    Quaternion level() const { return _level; }

private:
    void kick(World& world, DieResult* results, UniformSource& source);

    KickPlan       _kick;
    ShakeDetector  _detector;
    StillSpan      _still;
    ReadingAverage _average;
    Quaternion     _level{1, 0, 0, 0};
    bool           _hasLevel = false;
    bool           _shaking = false;
    /** @brief Whether every die was locked at the last sample, so the sample that sees them lock is known. */
    bool           _locked = false;
    Reorientation  _turns[MAX_DICE] = {};
};

#endif // SHAKE_ROLL_H

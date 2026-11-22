#include "shake_roll.h"

#include "../physics/tray_gravity.h"
#include "dice_roll.h"

bool ShakeRoll::addSample(const ImuSample& sample, World& world, DieResult* results, UniformSource& source) {
    _detector.add(sample);
    _still.add(sample);
    _average.add(sample);
    // ADR-0014: in flight the level follows the hand, which reclines to read once the shake stops;
    // a level frozen at the hold before the shake slid the dice to the top of an upright StickC.
    // Locked, the level is a still span found since the dice locked; until one is, it stays where
    // the roll left it. An older span, from before the roll or held while it flew, is an angle the
    // hand has since left, and levelling to it tilts the tray under resting dice (slice 0018).
    const bool locked = everyDieLocked(results, world.dieCount);
    if (locked && !_locked) _still.forget();
    _locked = locked;
    if (locked) {
        if (_still.found()) {
            _level = levelFrom(_still.latestMeanG());
            _hasLevel = true;
        }
    } else if (_hasLevel && length(_average.meanG()) >= LEVEL_MIN_AVERAGE_G) {
        _level = levelFrom(_average.meanG());
    }
    if (_hasLevel) world.gravity = trayGravity(_level, sample.accelG);

    const bool wasShaking = _shaking;
    _shaking = _detector.shaking();
    if (!_shaking || wasShaking) return false;
    kick(world, results, source);
    return true;
}

void ShakeRoll::kick(World& world, DieResult* results, UniformSource& source) {
    for (uint8_t die = 0; die < world.dieCount; die++) {
        results[die] = startRoll();
        if (_kick.kind == ShakeKick::Reorient) world.dice[die].orientation = uniformOrientation(source);
        if (_kick.kind != ShakeKick::None) throwDie(world.dice[die], source);
        if (_kick.kind == ShakeKick::Spin) _turns[die] = startReorientation(world.dice[die], _kick.spinS, source);
    }
}

uint8_t ShakeRoll::step(World& world, DieResult* results, UniformSource& source, const RestThresholds& thresholds) {
    for (uint8_t die = 0; die < world.dieCount; die++) continueReorientation(_turns[die], world.dice[die]);
    stepWorld(world, PRODUCT_TRAY);
    // A die resting a moment in the middle of a shake is not a result (ADR-0010).
    if (_shaking) return 0;
    return advanceResults(world, results, source, thresholds);
}

void ShakeRoll::launch(World& world, DieResult* results, uint8_t dieCount, UniformSource& source) {
    for (Reorientation& turn : _turns) turn = {};
    launchRoll(world, results, dieCount, source);
}

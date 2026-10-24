#include "shake_roll.h"

#include "../physics/tray_gravity.h"
#include "dice_roll.h"

bool ShakeRoll::addSample(const ImuSample& sample, World& world, DieResult* results, UniformSource& source) {
    _detector.add(sample);
    _still.add(sample);
    // A roll in flight keeps the level it started with: a hold after the shake at another angle
    // tilts the tray, as it would a real one, until the last die locks.
    if (_still.found() && everyDieLocked(results, world.dieCount)) {
        _level = levelFrom(_still.latestMeanG());
        _hasLevel = true;
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

#ifndef IMU_HAL_H
#define IMU_HAL_H

#include "startable.h"
#include "../imu/imu_sample.h"
#include "../imu/imu_settings.h"

/**
 * @brief The board's IMU, delivering raw samples on the device frame (ADR-0001).
 */
class ImuHAL : public Startable {
public:
    /**
     * @brief Choose the range and rate begin() sets up. Required before begin(), which
     *        fails without it: the settings are the probe's or an ADR's decision
     *        (slice 0004), so no driver carries a default that could stand in for one.
     */
    void configure(const ImuSettings& settings) {
        _settings = settings;
        _configured = true;
    }

    /**
     * @brief Read the latest sample from the chip.
     * @return false on any bus error (short read or NACK); @p sample is then left
     *         untouched, so a failed read can never pass for a stale or zero sample.
     */
    virtual bool read(ImuSample& sample) = 0;

    /**
     * @brief Whether the chip has converted a sample since the last read().
     *
     * Reading on the chip's own clock takes each sample exactly once; a timer of ours
     * drifts against it and would duplicate or skip samples, which the IMU probe measures
     * (slice 0004).
     * @return false on a bus error, with @p available left untouched.
     */
    virtual bool hasNewSample(bool& available) = 0;

protected:
    ImuSettings _settings{};
    bool _configured = false;
};

#endif // IMU_HAL_H

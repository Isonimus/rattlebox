#ifndef IMU_HAL_H
#define IMU_HAL_H

#include "startable.h"
#include "../imu/imu_sample.h"

/**
 * @brief The board's IMU, delivering raw samples on the device frame (ADR-0001).
 */
class ImuHAL : public Startable {
public:
    /**
     * @brief Read the latest sample from the chip.
     * @return false on any bus error (short read or NACK); @p sample is then left
     *         untouched, so a failed read can never pass for a stale or zero sample.
     */
    virtual bool read(ImuSample& sample) = 0;
};

#endif // IMU_HAL_H

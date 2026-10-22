#ifndef BOOT_STEPS_H
#define BOOT_STEPS_H

#include <stddef.h>
#include "../hal/startable.h"

struct BootStep {
    const char* subsystem;
    Startable&  part;
};

struct BootOutcome {
    /** @brief The step that failed, or nullptr when every step started. */
    const BootStep* failedStep;
    const char*     failureReason;
};

/** @brief Called as each step starts, so a hang in a later step still leaves the log. */
using BootStepStarted = void (*)(const BootStep& step);

/**
 * @brief Start each step in order and stop at the first that fails (slice 0003).
 * A later step never starts once one has failed: it may depend on the failed one, and
 * the device must not run on hardware that is only partly up.
 */
BootOutcome runBootSteps(const BootStep* steps, size_t count, BootStepStarted onStarted);

#endif // BOOT_STEPS_H

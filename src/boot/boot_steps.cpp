#include "boot_steps.h"

namespace {
// A part that fails without saying why is itself a defect, but boot must still stop
// and say which part it was.
const char* const MISSING_FAILURE_REASON = "begin() failed without giving a reason";
} // namespace

BootOutcome runBootSteps(const BootStep* steps, size_t count, BootStepStarted onStarted) {
    for (size_t i = 0; i < count; i++) {
        const BootStep& step = steps[i];
        if (!step.part.begin()) {
            const char* reason = step.part.failureReason();
            return {&step, reason != nullptr ? reason : MISSING_FAILURE_REASON};
        }
        onStarted(step);
    }
    return {nullptr, nullptr};
}

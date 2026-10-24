#include "frame_pacer.h"

FramePace paceFrame(uint32_t deadlineUs, uint32_t nowUs) {
    const int32_t earlyUs = static_cast<int32_t>(deadlineUs - nowUs);
    if (earlyUs >= 0) return {static_cast<uint32_t>(earlyUs), deadlineUs + FRAME_PERIOD_US, false};
    if (static_cast<uint32_t>(-earlyUs) < FRAME_PERIOD_US) return {0, deadlineUs + FRAME_PERIOD_US, true};
    return {0, nowUs + FRAME_PERIOD_US, true};
}

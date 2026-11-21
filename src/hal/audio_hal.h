#ifndef AUDIO_HAL_H
#define AUDIO_HAL_H

#include <stdint.h>
#include "startable.h"

/**
 * @brief The board's sound: a clack for each hit in the physics (slice 0013). Dragon Radar's
 * melody calls stay behind; nothing here plays a tune.
 */
class AudioHAL : public Startable {
public:
    /** @brief Play a clack of @p loudness in (0, 1] now, without waiting for the sound. */
    virtual void clack(float loudness) = 0;

    /**
     * @brief The longest any block of samples took to render since the last call, in µs, and start
     * over. 0 on a board whose sound hardware makes the waveform itself.
     */
    virtual uint32_t takeLongestRenderUs() = 0;
};

#endif // AUDIO_HAL_H

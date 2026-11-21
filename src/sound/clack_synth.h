#ifndef CLACK_SYNTH_H
#define CLACK_SYNTH_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Clacks as band-passed white noise under a decaying envelope (slice 0013).
 *
 * Every clack raises one shared envelope by its loudness, clipped at full: the filter is linear,
 * so overlapping clacks summed through one filter sound as each through its own, with no voices
 * to allocate or steal.
 */
class ClackSynth {
public:
    /** @brief @p amplitude is the sample value of a full-loudness clack's noise before filtering. */
    ClackSynth(uint32_t sampleRateHz, int16_t amplitude);

    /** @brief Start a clack of @p loudness in (0, 1] at the next sample. */
    void clack(float loudness);

    /** @brief Render @p frames stereo frames, the same sample left and right, into @p interleaved. */
    void fill(int16_t* interleaved, size_t frames);

private:
    float nextNoise();

    float    _amplitude;
    float    _decayPerSample;
    float    _envelope = 0;
    uint32_t _noiseState;
    // A band-pass biquad, direct form I; its b1 is zero.
    float _b0, _b2, _a1, _a2;
    float _x1 = 0, _x2 = 0, _y1 = 0, _y2 = 0;
};

#endif // CLACK_SYNTH_H

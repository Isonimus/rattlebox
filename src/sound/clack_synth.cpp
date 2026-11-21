#include "clack_synth.h"

#include <math.h>

namespace {

// Feel values, tuned by ear on the Cardputer's speaker: where a plastic die's knock sits, how wide
// it is, and how fast it dies. The decay is the envelope's time constant, to 1/e.
constexpr float CLACK_CENTRE_HZ = 3000.0f;
constexpr float CLACK_Q = 1.2f;
constexpr float CLACK_DECAY_S = 0.008f;
// Under this the envelope and the filter's ringing are inaudible, far below one 16-bit step at any
// amplitude; cut to zero there, they stay out of float subnormals, which are slow to compute.
constexpr float SILENT = 1e-6f;
// Any nonzero seed works for xorshift; this one is the generator's published example.
constexpr uint32_t NOISE_SEED = 2463534242u;
constexpr float NOISE_SCALE = 1.0f / 2147483648.0f;

} // namespace

ClackSynth::ClackSynth(uint32_t sampleRateHz, int16_t amplitude)
    : _amplitude(amplitude), _decayPerSample(expf(-1.0f / (CLACK_DECAY_S * sampleRateHz))), _noiseState(NOISE_SEED) {
    // The band-pass of the Audio EQ Cookbook (R. Bristow-Johnson), 0 dB at its centre.
    const float w0 = 2 * static_cast<float>(M_PI) * CLACK_CENTRE_HZ / sampleRateHz;
    const float alpha = sinf(w0) / (2 * CLACK_Q);
    const float a0 = 1 + alpha;
    _b0 = alpha / a0;
    _b2 = -alpha / a0;
    _a1 = -2 * cosf(w0) / a0;
    _a2 = (1 - alpha) / a0;
}

void ClackSynth::clack(float loudness) {
    _envelope += loudness;
    if (_envelope > 1) _envelope = 1;
}

float ClackSynth::nextNoise() {
    // Marsaglia's xorshift32: noise needs no statistical quality, only to be cheap and flat.
    _noiseState ^= _noiseState << 13;
    _noiseState ^= _noiseState >> 17;
    _noiseState ^= _noiseState << 5;
    return static_cast<int32_t>(_noiseState) * NOISE_SCALE;
}

void ClackSynth::fill(int16_t* interleaved, size_t frames) {
    for (size_t frame = 0; frame < frames; frame++) {
        const float input = _envelope == 0 ? 0 : nextNoise() * _envelope;
        float output = _b0 * input + _b2 * _x2 - _a1 * _y1 - _a2 * _y2;
        if (fabsf(output) < SILENT && _envelope == 0) output = 0;
        _x2 = _x1;
        _x1 = input;
        _y2 = _y1;
        _y1 = output;
        _envelope *= _decayPerSample;
        if (_envelope < SILENT) _envelope = 0;

        float sample = output * _amplitude;
        // White noise at full loudness peaked at 0.98 of the amplitude over 10^8 samples, but the
        // filter can gain up to 1.29 (its impulse response's absolute sum), and a float past
        // int16's range has no defined conversion: it clips, so no input can wrap it.
        if (sample > INT16_MAX) sample = INT16_MAX;
        if (sample < INT16_MIN) sample = INT16_MIN;
        interleaved[2 * frame] = interleaved[2 * frame + 1] = static_cast<int16_t>(sample);
    }
}

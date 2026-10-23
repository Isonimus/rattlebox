#ifndef SEEDED_SOURCE_H
#define SEEDED_SOURCE_H

#include <stdint.h>
#include "../../physics/launch.h"

/**
 * @brief The host's launch randomness: SplitMix64, so a seed repeats a run exactly.
 *
 * SplitMix64 passes BigCrush and needs one 64-bit word of state. The device never uses it: its
 * launches come from the hardware RNG (ADR-0002).
 */
class SeededSource : public UniformSource {
public:
    explicit SeededSource(uint64_t seed) : _state(seed) {}

    float nextUnit() override {
        // The top 24 bits fill a float's mantissa exactly, so every value is in [0, 1).
        constexpr int FLOAT_MANTISSA_BITS = 24;
        constexpr float UNIT_PER_STEP = 1.0f / (1u << FLOAT_MANTISSA_BITS);
        return static_cast<float>(next() >> (64 - FLOAT_MANTISSA_BITS)) * UNIT_PER_STEP;
    }

private:
    uint64_t next() {
        _state += 0x9E3779B97F4A7C15ull;
        uint64_t z = _state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    uint64_t _state;
};

#endif // SEEDED_SOURCE_H

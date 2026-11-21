#include <math.h>
#include <unity.h>
#include "../../src/sound/clack_loudness.h"
#include "../../src/sound/clack_synth.h"
#include "../../src/sound/clack_synth.cpp"

// The clacks' loudness and their sound, as the Cardputer renders them (slice 0013).

static const uint32_t SAMPLE_RATE_HZ = 48000;
static const int16_t AMPLITUDE = 8000;
// Long past any clack: 0.25 s is about thirty of the envelope's time constants.
static const size_t FRAMES = SAMPLE_RATE_HZ / 4;
// The first 5 ms: one of the Cardputer's blocks, where a clack is loudest.
static const size_t ONSET_FRAMES = SAMPLE_RATE_HZ / 200;
// The same noise scaled is the same sound scaled; only the rounding to 16 bits differs.
static const float ENERGY_TOLERANCE = 0.01f;

static int16_t buffer[2 * FRAMES];

/** @brief The sum of squares of the left channel's first @p frames samples. */
static double energy(size_t frames) {
    double sum = 0;
    for (size_t frame = 0; frame < frames; frame++) sum += static_cast<double>(buffer[2 * frame]) * buffer[2 * frame];
    return sum;
}

static double clackEnergy(float loudness) {
    ClackSynth synth(SAMPLE_RATE_HZ, AMPLITUDE);
    synth.clack(loudness);
    synth.fill(buffer, FRAMES);
    return energy(FRAMES);
}

void test_no_clack_is_silence() {
    ClackSynth synth(SAMPLE_RATE_HZ, AMPLITUDE);
    synth.fill(buffer, FRAMES);
    TEST_ASSERT_EQUAL_DOUBLE(0, energy(FRAMES));
}

void test_a_clack_is_a_burst_that_dies_to_silence() {
    ClackSynth synth(SAMPLE_RATE_HZ, AMPLITUDE);
    synth.clack(1);
    synth.fill(buffer, FRAMES);
    const double onset = energy(ONSET_FRAMES);
    TEST_ASSERT_TRUE(onset > 0);
    // Nearly all of it in the first 5 ms, and the last half of the buffer exactly silent.
    TEST_ASSERT_TRUE(onset > 0.5 * energy(FRAMES));
    for (size_t frame = FRAMES / 2; frame < FRAMES; frame++) TEST_ASSERT_EQUAL_INT16(0, buffer[2 * frame]);
}

void test_both_channels_play_the_same_sample() {
    ClackSynth synth(SAMPLE_RATE_HZ, AMPLITUDE);
    synth.clack(1);
    synth.fill(buffer, ONSET_FRAMES);
    for (size_t frame = 0; frame < ONSET_FRAMES; frame++) TEST_ASSERT_EQUAL_INT16(buffer[2 * frame], buffer[2 * frame + 1]);
}

void test_a_clack_scales_with_its_loudness() {
    // Amplitude in proportion, so energy by its square.
    const double full = clackEnergy(1);
    const double quarter = clackEnergy(0.25f);
    TEST_ASSERT_DOUBLE_WITHIN(ENERGY_TOLERANCE * full / 16, full / 16, quarter);
}

void test_overlapping_clacks_add_and_clip_at_full() {
    ClackSynth twoHalves(SAMPLE_RATE_HZ, AMPLITUDE);
    twoHalves.clack(0.3f);
    twoHalves.clack(0.3f);
    twoHalves.fill(buffer, FRAMES);
    const double added = energy(FRAMES);
    TEST_ASSERT_DOUBLE_WITHIN(ENERGY_TOLERANCE * added, clackEnergy(0.6f), added);

    ClackSynth twoLoud(SAMPLE_RATE_HZ, AMPLITUDE);
    twoLoud.clack(0.8f);
    twoLoud.clack(0.8f);
    twoLoud.fill(buffer, FRAMES);
    const double clipped = energy(FRAMES);
    TEST_ASSERT_DOUBLE_WITHIN(ENERGY_TOLERANCE * clipped, clackEnergy(1), clipped);
}

void test_a_clack_in_a_dying_one_rises_again() {
    ClackSynth synth(SAMPLE_RATE_HZ, AMPLITUDE);
    synth.clack(1);
    synth.fill(buffer, FRAMES);
    synth.clack(1);
    synth.fill(buffer, ONSET_FRAMES);
    TEST_ASSERT_TRUE(energy(ONSET_FRAMES) > 0);
}

void test_no_clack_at_or_below_the_floor() {
    TEST_ASSERT_EQUAL_FLOAT(0, clackLoudness(0));
    TEST_ASSERT_EQUAL_FLOAT(0, clackLoudness(CLACK_MIN_IMPULSE));
    TEST_ASSERT_TRUE(clackLoudness(CLACK_MIN_IMPULSE * 1.01f) > 0);
}

void test_loudness_is_in_proportion_to_the_impulse_up_to_full() {
    TEST_ASSERT_EQUAL_FLOAT(0.5f, clackLoudness(CLACK_FULL_IMPULSE / 2));
    TEST_ASSERT_EQUAL_FLOAT(1, clackLoudness(CLACK_FULL_IMPULSE));
    TEST_ASSERT_EQUAL_FLOAT(1, clackLoudness(CLACK_FULL_IMPULSE * 4));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_no_clack_is_silence);
    RUN_TEST(test_a_clack_is_a_burst_that_dies_to_silence);
    RUN_TEST(test_both_channels_play_the_same_sample);
    RUN_TEST(test_a_clack_scales_with_its_loudness);
    RUN_TEST(test_overlapping_clacks_add_and_clip_at_full);
    RUN_TEST(test_a_clack_in_a_dying_one_rises_again);
    RUN_TEST(test_no_clack_at_or_below_the_floor);
    RUN_TEST(test_loudness_is_in_proportion_to_the_impulse_up_to_full);
    return UNITY_END();
}

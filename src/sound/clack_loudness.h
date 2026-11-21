#ifndef CLACK_LOUDNESS_H
#define CLACK_LOUDNESS_H

/**
 * @brief No clack at or below this impulse (mass·mm/s). Twice the largest hit the clack probe
 * found among dice at rest, which a contact makes holding a die's weight for a step (40.9), and
 * low enough that 84 to 87% of a shake's 50 ms windows still hold a clack (slice 0013).
 */
constexpr float CLACK_MIN_IMPULSE = 50.0f;
/** @brief Full loudness from this impulse up: about the 99th percentile of the shakes' hits (slice 0013). */
constexpr float CLACK_FULL_IMPULSE = 5000.0f;

/**
 * @brief A hit's loudness in [0, 1], 0 for no clack: proportional to its impulse, as a knock's
 * amplitude follows the momentum it carries, so the quietest clack plays 40 dB under the loudest.
 */
constexpr float clackLoudness(float impulse) {
    if (impulse <= CLACK_MIN_IMPULSE) return 0;
    return impulse >= CLACK_FULL_IMPULSE ? 1 : impulse / CLACK_FULL_IMPULSE;
}

#endif // CLACK_LOUDNESS_H

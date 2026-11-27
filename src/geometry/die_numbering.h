#ifndef DIE_NUMBERING_H
#define DIE_NUMBERING_H

#include <stdint.h>

/**
 * @brief The most bodies one die throws. A d% is two and no die on the roadmap is more
 * (`docs/roadmap.md`), so this sizes the per-body declaration in `DieKind` (ADR-0034).
 */
constexpr uint8_t MAX_BODIES_PER_DIE = 2;

/** @brief The longest label a face carries, without its terminator: a tens die's "90". */
constexpr uint8_t NUMERAL_LABEL_MAX_CHARS = 2;

/** @brief How many outcomes a percentile roll has, and so what 00 and 0 reads as. */
constexpr uint8_t PERCENTILE_OUTCOMES = 100;

/** @brief What one step up a tens body is worth. */
constexpr uint8_t PERCENTILE_TENS_STEP = 10;

/**
 * @brief What one body's numerals mean (ADR-0034).
 *
 * It sits beside `die_shape.h` rather than inside it because the renderer and the roll both read
 * it while neither owns it, and geometry is the layer below both. It is deliberately *not* a
 * field of `DieShape`: a shape's numerals mean themselves, and the whole point of this
 * declaration is that a percentile body's do not.
 *
 * A shape's numerals run 1 to N for every die, because that is the one numbering rule every die
 * shares and `test/test_geometry` holds it. A percentile die still needs to show 00 to 90 and 0 to
 * 9, so what a numeral *means* is declared here, beside the body, and never written into the
 * geometry. Slice 0027 refused to number a D10 0 to 9 in its shape for the same reason.
 */
enum class Numbering : uint8_t {
    /** @brief The numeral is the result. Zero, so a body that forgets to say reads as it always did. */
    Plain,
    /** @brief The tens body of a d%: 1 to 10 shows 00 to 90. */
    PercentileTens,
    /** @brief The units body of a d%: 1 to 10 shows 0 to 9. */
    PercentileUnits,
};

/**
 * @brief The label @p numeral shows under @p numbering, as a string that outlives every caller.
 *
 * A numeral outside 1 to 10 under a percentile numbering, or 0 under any, aborts: the shapes
 * guarantee 1 to N, so anything else is a corrupted shape and not a label to guess at.
 *
 * It returns a constant rather than formatting into a caller's buffer because the renderer asks
 * for one per visible face, per die, per frame. Formatting them cost 120 us of a StickC frame,
 * measured with the render probe across slice 0035: its geometry phase went from 201 us to 329,
 * and back to 209 with the formatting removed. A label is a pure function of a numbering and a
 * numeral over at most `DIE_MAX_FACES` values, so there is nothing to compute at all.
 */
const char* numeralLabel(Numbering numbering, uint8_t numeral);

#endif // DIE_NUMBERING_H

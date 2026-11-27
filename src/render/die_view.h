#ifndef DIE_VIEW_H
#define DIE_VIEW_H

#include <stdint.h>
#include "../geometry/die_numbering.h"
#include "../geometry/die_shape.h"
#include "../geometry/rotation.h"

/** @brief An 8-bit-per-channel colour, before shading and packing. */
struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

/** @brief How faces are lit: one fixed light, plus a floor so no face goes black. */
struct Lighting {
    /** @brief Unit vector from the die toward the light, tray frame. */
    Vec3  towardLight;
    /** @brief Brightness of a front face turned fully away from the light, 0 to 1. */
    float ambient;
};

/**
 * @brief How far toward the camera a face's normal must turn before the face is drawn. A regular
 * solid has faces exactly edge-on at rest (four of the D20's), and float rounding puts their normals
 * a hair either side of perpendicular, by up to 4.8e-9 where multiply-adds are fused, so a test at
 * exactly zero would draw some of them on one compiler and not another. A face this close to edge-on
 * is 1e-4 of its width across on screen: under a hundredth of a pixel for any die the tray draws.
 */
constexpr float FACING_EDGE_ON = 1e-4f;

/** @brief One face as drawn: a screen-space convex polygon, its colour, and its numeral if it shows one. */
struct ScreenFace {
    int16_t  x[FACE_MAX_CORNERS];
    int16_t  y[FACE_MAX_CORNERS];
    uint8_t  cornerCount;
    uint16_t color565;
    bool     showsNumeral;
    int16_t  numeralX;
    int16_t  numeralY;
    /**
     * @brief What the face shows, already resolved through the die's numbering: "7" for a plain
     * die, "00" to "90" for a percentile tens body. Text and not a number because "00" is two
     * characters and not zero (ADR-0034).
     */
    char     numeralText[NUMERAL_LABEL_MAX_CHARS + 1];
};

/** @brief The front faces of one die, in no particular order: a convex die's front faces never overlap. */
struct DieView {
    ScreenFace faces[DIE_MAX_FACES];
    uint8_t    faceCount;
};

/**
 * @brief A die on a top-down orthographic camera: the tray's x is the screen's x, the
 * tray's y is the screen's up, and the camera looks down the tray's −z.
 *
 * A face is drawn only if its normal turns toward the camera by more than FACING_EDGE_ON; it shows its numeral
 * only if that component is at least @p numeralMinFacing, the cosine of the widest angle
 * from the camera at which a numeral still reads on so small a face. @p pxPerUnit scales the
 * shape's length unit to pixels, so the physics can keep real millimetres. @p numbering says what
 * this body's numerals mean, so a percentile body draws 00 to 90 or 0 to 9 where the shape still
 * holds 1 to 10 (ADR-0034).
 *
 * A die that declares NumeralReading::Apex ignores @p numeralMinFacing, because the rule it
 * needs is not an angle (ADR-0033): its result is on the face it rests on, which faces away, and
 * the digit is drawn on every visible face meeting the raised corner. The raised corner is found
 * from that resting face, so what the screen shows can never disagree with what the die reads.
 */
void viewDie(const DieShape& shape, Numbering numbering, float pxPerUnit, const Rotation& orientation,
             Vec3 centrePx, Rgb base, const Lighting& lighting, float numeralMinFacing, DieView& view);

/** @brief Scale @p base by @p brightness (0 to 1) and pack it as the panel's RGB565. */
uint16_t shadeRgb565(Rgb base, float brightness);

#endif // DIE_VIEW_H

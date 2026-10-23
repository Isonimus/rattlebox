#ifndef DIE_VIEW_H
#define DIE_VIEW_H

#include <stdint.h>
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

/** @brief One face as drawn: a screen-space convex polygon, its colour, and its numeral if it shows one. */
struct ScreenFace {
    int16_t  x[FACE_MAX_CORNERS];
    int16_t  y[FACE_MAX_CORNERS];
    uint8_t  cornerCount;
    uint16_t color565;
    bool     showsNumeral;
    int16_t  numeralX;
    int16_t  numeralY;
    uint8_t  numeral;
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
 * A face is drawn only if its normal has a component toward the camera; it shows its numeral
 * only if that component is at least @p numeralMinFacing, the cosine of the widest angle
 * from the camera at which a numeral still reads on so small a face. @p pxPerUnit scales the
 * shape's length unit to pixels, so the physics can keep real millimetres.
 */
void viewDie(const DieShape& shape, float pxPerUnit, const Rotation& orientation, Vec3 centrePx, Rgb base,
             const Lighting& lighting, float numeralMinFacing, DieView& view);

/** @brief Scale @p base by @p brightness (0 to 1) and pack it as the panel's RGB565. */
uint16_t shadeRgb565(Rgb base, float brightness);

#endif // DIE_VIEW_H

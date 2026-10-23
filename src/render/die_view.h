#ifndef DIE_VIEW_H
#define DIE_VIEW_H

#include <stdint.h>
#include "../geometry/d20_mesh.h"
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

constexpr int TRIANGLE_CORNERS = 3;

/** @brief One face as drawn: a screen-space triangle, its colour, and its numeral if it shows one. */
struct ScreenFace {
    int16_t  x[TRIANGLE_CORNERS];
    int16_t  y[TRIANGLE_CORNERS];
    uint16_t color565;
    bool     showsNumeral;
    int16_t  numeralX;
    int16_t  numeralY;
    uint8_t  numeral;
};

/** @brief The front faces of one die, in no particular order: a convex die's front faces never overlap. */
struct DieView {
    ScreenFace faces[D20_FACE_COUNT];
    uint8_t    faceCount;
};

/**
 * @brief A die on a top-down orthographic camera: the tray's x is the screen's x, the
 * tray's y is the screen's up, and the camera looks down the tray's −z.
 *
 * A face is drawn only if its normal has a component toward the camera; it shows its numeral
 * only if that component is at least @p numeralMinFacing, the cosine of the widest angle
 * from the camera at which a numeral still reads on so small a face.
 */
void viewDie(const D20Mesh& mesh, const Rotation& orientation, Vec3 centrePx, Rgb base, const Lighting& lighting,
             float numeralMinFacing, DieView& view);

/** @brief Scale @p base by @p brightness (0 to 1) and pack it as the panel's RGB565. */
uint16_t shadeRgb565(Rgb base, float brightness);

#endif // DIE_VIEW_H

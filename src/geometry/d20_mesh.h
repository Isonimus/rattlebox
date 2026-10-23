#ifndef D20_MESH_H
#define D20_MESH_H

#include <stdint.h>
#include "vec3.h"

constexpr uint8_t D20_VERTEX_COUNT = 12;
constexpr uint8_t D20_FACE_COUNT = 20;

/** @brief A triangle's vertex indices, counter-clockwise seen from outside the die. */
struct FaceVertices {
    uint8_t a;
    uint8_t b;
    uint8_t c;
};

/**
 * @brief A D20 (regular icosahedron) centred on its body's origin, in pixels.
 *
 * The physics core will collide these vertices with the tray (ADR-0002), and the renderer
 * draws these faces, so both read the same body.
 */
struct D20Mesh {
    Vec3         vertices[D20_VERTEX_COUNT];
    FaceVertices faces[D20_FACE_COUNT];
    /** @brief Outward unit normal of each face. */
    Vec3         normals[D20_FACE_COUNT];
    /** @brief The number on each face; opposite faces sum to 21, as on a real D20. */
    uint8_t      numerals[D20_FACE_COUNT];
};

D20Mesh makeD20(float edgeLength);

#endif // D20_MESH_H

#ifndef DIE_SHAPE_H
#define DIE_SHAPE_H

#include <stdint.h>
#include "vec3.h"

// The largest dice built so far: the D20's 12 vertices and 20 faces, and the D6's square faces.
// A D12's pentagons (v0.2) raise the corner limit when it comes.
constexpr uint8_t DIE_MAX_VERTICES = 12;
constexpr uint8_t DIE_MAX_FACES = 20;
constexpr uint8_t FACE_MAX_CORNERS = 4;

/** @brief One flat face: a convex polygon, its corners counter-clockwise seen from outside the die. */
struct DieFace {
    uint8_t corners[FACE_MAX_CORNERS];
    uint8_t cornerCount;
    /** @brief Outward unit normal. */
    Vec3    normal;
    /** @brief The number printed on the face; opposite faces sum to one more than the face count. */
    uint8_t numeral;
};

/**
 * @brief A die's body, centred on its origin, in whatever length unit it was built in.
 *
 * The physics collides its vertices with the tray (ADR-0002) and reads the result from its
 * faces, and the renderer draws the same faces, so the picture and the result are one body.
 */
struct DieShape {
    Vec3    vertices[DIE_MAX_VERTICES];
    uint8_t vertexCount;
    DieFace faces[DIE_MAX_FACES];
    uint8_t faceCount;
    /** @brief Distance from the centre to every vertex: the bounding sphere dice collide with. */
    float   circumradius;
    /**
     * @brief Moment of inertia per unit mass, in length units squared, about any axis through
     * the centre: a uniform Platonic solid's inertia is the same about every axis.
     */
    float   inertiaPerMass;
};

DieShape makeD6(float edgeLength);
DieShape makeD20(float edgeLength);

/** @brief The centre of a face's corners, which for a regular polygon is its centroid. */
Vec3 faceCentre(const DieShape& shape, uint8_t face);

#endif // DIE_SHAPE_H

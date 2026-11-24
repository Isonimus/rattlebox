#ifndef DIE_SHAPE_H
#define DIE_SHAPE_H

#include <stdint.h>
#include "vec3.h"

// One set of limits sized for the largest die the product builds, raised when a new die needs
// more (ADR-0030): the D12's 20 vertices and pentagons, and the D20's 20 faces. They are not
// only a ceiling. They size ContactMemory, world.cpp's contact array, HitFinder's record and
// DieShape itself, so raising them costs every die: measured at 8,448 bytes of static RAM on
// both boards and under 2% of a frame, which is what ADR-0030 weighed. The D4 and the d% still
// to come both fit inside these (docs/roadmap.md).
constexpr uint8_t DIE_MAX_VERTICES = 20;
constexpr uint8_t DIE_MAX_FACES = 20;
constexpr uint8_t FACE_MAX_CORNERS = 5;

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
    /**
     * @brief The bounding sphere dice collide with (ADR-0002): the distance to the furthest
     * vertex, which for every die but the D10 is the distance to all of them.
     */
    float   circumradius;
    /**
     * @brief Moment of inertia per unit mass, in length units squared, about any axis through
     * the centre: a uniform Platonic solid's inertia is the same about every axis.
     *
     * The D10 is not Platonic, and is built at the one proportion of its family whose inertia
     * is isotropic anyway, so that this stays exact for it too (slice 0027).
     */
    float   inertiaPerMass;
    /**
     * @brief How long this die must stay below the rest speeds before its result locks.
     *
     * Per die, beside the inertia, because how long a body can balance on an edge before it
     * goes over is a fact about that body: four times the longest pause its own trace measured
     * (ADR-0028). A single hold made every die wait out the worst of them.
     */
    float   holdS;
};

DieShape makeD6(float edgeLength);
DieShape makeD8(float edgeLength);
/** @brief A pentagonal trapezohedron. Sized by circumradius: a kite has no single edge length. */
DieShape makeD10(float circumradius);
DieShape makeD12(float edgeLength);
DieShape makeD20(float edgeLength);

/**
 * @brief The centre of a face's corners, which for a regular polygon is its centroid.
 *
 * Not for a kite: a D10's face has its corner mean nearer the narrow end than its area centroid,
 * and the renderer places a numeral at the same corner mean. That was checked on both boards and
 * the numerals read, the widest of them a two-digit 10, so it is left alone deliberately
 * (slice 0027).
 */
Vec3 faceCentre(const DieShape& shape, uint8_t face);

#endif // DIE_SHAPE_H

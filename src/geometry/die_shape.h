#ifndef DIE_SHAPE_H
#define DIE_SHAPE_H

#include <stdint.h>
#include "vec3.h"

// One set of limits sized for the largest die the product builds, raised when a new die needs
// more (ADR-0030): the D12's 20 vertices and pentagons, and the D20's 20 faces. They are not
// only a ceiling. They size ContactMemory, world.cpp's contact array, HitFinder's record and
// DieShape itself, so raising them costs every die: measured at 8,448 bytes of static RAM on
// both boards and under 2% of a frame, which is what ADR-0030 weighed. The D4 shipped inside them
// unchanged, and the d% still to come fits too (docs/roadmap.md).
constexpr uint8_t DIE_MAX_VERTICES = 20;
constexpr uint8_t DIE_MAX_FACES = 20;
constexpr uint8_t FACE_MAX_CORNERS = 5;

/**
 * @brief Where a die's result is read, which is not the same question for every solid (ADR-0033).
 *
 * Five of the six dice are centrally symmetric: every face has another exactly opposed to it, so
 * the die rests on one face and presents another, and reading the face toward the glass is the
 * whole rule. A tetrahedron has no opposite face. It rests on a face and raises the vertex
 * opposite that face, and the numeral a player reads is the resting face's, written at the apex
 * above it, as it is on a printed D4.
 */
enum class NumeralReading : uint8_t {
    /** @brief The numeral on the face most nearly toward the glass. Zero, so a shape that forgets
     * to say reads the way every die did before the D4. */
    TopFace,
    /** @brief The numeral on the face the die rests on, drawn at the apex standing over it. */
    Apex,
};

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
    /**
     * @brief Where this die's result is read (ADR-0033).
     *
     * A claim about the solid, not a preference: TopFace asserts every face has one exactly
     * opposed to it, Apex asserts every vertex has. Standing invariant #7, and test_geometry
     * checks each catalog entry's declaration against its own geometry.
     */
    NumeralReading reading;
};

/** @brief A regular tetrahedron. The one die read at its apex rather than its top face (ADR-0033). */
DieShape makeD4(float edgeLength);
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

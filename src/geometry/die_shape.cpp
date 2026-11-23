#include "die_shape.h"

namespace {

constexpr float GOLDEN_RATIO = 1.6180339887f;

// ---- D6

constexpr uint8_t D6_VERTEX_COUNT = 8;
constexpr uint8_t D6_FACE_COUNT = 6;
constexpr uint8_t SQUARE_CORNERS = 4;
// A uniform cube of edge a: I = m a² / 6.
constexpr float CUBE_INERTIA_PER_MASS_EDGE_SQUARED = 1.0f / 6;

// Vertex i has bit 0 set for +x, bit 1 for +y and bit 2 for +z.
constexpr uint8_t X_BIT = 1;
constexpr uint8_t Y_BIT = 2;
constexpr uint8_t Z_BIT = 4;

struct SquareFace {
    uint8_t corners[SQUARE_CORNERS];
    uint8_t numeral;
};

// Opposite faces carry 1 and 6, 2 and 5, 3 and 4, as on a real D6.
constexpr SquareFace D6_FACES[D6_FACE_COUNT] = {
    {{4, 5, 7, 6}, 1}, {{0, 2, 3, 1}, 6}, // +z, −z
    {{1, 3, 7, 5}, 2}, {{0, 4, 6, 2}, 5}, // +x, −x
    {{2, 6, 7, 3}, 3}, {{0, 1, 5, 4}, 4}, // +y, −y
};

float signedHalf(uint8_t vertex, uint8_t bit, float half) { return (vertex & bit) ? half : -half; }

// ---- D20

constexpr uint8_t D20_VERTEX_COUNT = 12;
constexpr uint8_t D20_FACE_COUNT = 20;
constexpr uint8_t TRIANGLE_CORNERS = 3;
// The vertices below, (0, ±1, ±φ) and their cyclic permutations, have edges of length 2.
constexpr float D20_UNIT_EDGE_LENGTH = 2.0f;
constexpr uint8_t HALF_VERTICES = D20_VERTEX_COUNT / 2;
constexpr uint8_t HALF_FACES = D20_FACE_COUNT / 2;
constexpr uint8_t D20_OPPOSITE_NUMERAL_SUM = D20_FACE_COUNT + 1;
// A uniform regular icosahedron of edge a: I = φ² m a² / 10.
constexpr float ICOSAHEDRON_INERTIA_PER_MASS_EDGE_SQUARED = GOLDEN_RATIO * GOLDEN_RATIO / 10;

// Vertex i + 6 is vertex i through the centre, so only half are listed.
constexpr Vec3 HALF_OF_VERTICES[HALF_VERTICES] = {
    {0, 1, GOLDEN_RATIO}, {0, -1, GOLDEN_RATIO}, {1, GOLDEN_RATIO, 0},
    {-1, GOLDEN_RATIO, 0}, {GOLDEN_RATIO, 0, 1}, {-GOLDEN_RATIO, 0, 1},
};

struct Triangle {
    uint8_t a;
    uint8_t b;
    uint8_t c;
};

// Face i + 10 is face i through the centre, so only half are listed: one of each pair of
// opposite faces, wound outward.
constexpr Triangle HALF_OF_FACES[HALF_FACES] = {
    {0, 1, 4}, {0, 5, 1}, {0, 2, 3}, {0, 4, 2}, {0, 3, 5},
    {1, 9, 4}, {1, 5, 8}, {1, 8, 9}, {2, 4, 11}, {3, 10, 5},
};

uint8_t oppositeVertex(uint8_t vertex) { return (vertex + HALF_VERTICES) % D20_VERTEX_COUNT; }

DieFace triangleFace(Triangle corners, uint8_t numeral) {
    return {{corners.a, corners.b, corners.c, 0}, TRIANGLE_CORNERS, {}, numeral};
}

// ---- D8

constexpr uint8_t D8_VERTEX_COUNT = 6;
constexpr uint8_t D8_FACE_COUNT = 8;
constexpr uint8_t D8_OPPOSITE_NUMERAL_SUM = D8_FACE_COUNT + 1;
constexpr float SQRT_2 = 1.4142135624f;
// A uniform regular octahedron of edge a: I = m a² / 10.
constexpr float OCTAHEDRON_INERTIA_PER_MASS_EDGE_SQUARED = 1.0f / 10;

// One vertex on each axis, either side: +x, −x, +y, −y, +z, −z.
constexpr uint8_t PLUS_X = 0;
constexpr uint8_t PLUS_Y = 2;
constexpr uint8_t PLUS_Z = 4;
constexpr uint8_t MINUS_OFFSET = 1;

struct Octant {
    bool    negativeX;
    bool    negativeY;
    bool    negativeZ;
    uint8_t numeral;
};

// One face per octant. The four above the equator carry 1 to 4; each one below carries nine minus
// the numeral of the face through the centre from it.
constexpr Octant D8_OCTANTS[D8_FACE_COUNT] = {
    {false, false, false, 1}, {true, false, false, 2}, {true, true, false, 3}, {false, true, false, 4},
    {true, true, true, D8_OPPOSITE_NUMERAL_SUM - 1}, {false, true, true, D8_OPPOSITE_NUMERAL_SUM - 2},
    {false, false, true, D8_OPPOSITE_NUMERAL_SUM - 3}, {true, false, true, D8_OPPOSITE_NUMERAL_SUM - 4},
};

uint8_t axisVertex(uint8_t plus, bool negative) { return negative ? plus + MINUS_OFFSET : plus; }

DieFace octantFace(const Octant& octant) {
    const uint8_t x = axisVertex(PLUS_X, octant.negativeX);
    const uint8_t y = axisVertex(PLUS_Y, octant.negativeY);
    const uint8_t z = axisVertex(PLUS_Z, octant.negativeZ);
    // x, y, z winds outward in the +++ octant; each axis mirrored reverses the winding.
    const bool mirrored = (octant.negativeX != octant.negativeY) != octant.negativeZ;
    return triangleFace(mirrored ? Triangle{x, z, y} : Triangle{x, y, z}, octant.numeral);
}

// ---- shared

/** @brief Fill in what follows from the vertices and faces: normals and the bounding radius. */
void finish(DieShape& shape) {
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        DieFace& face = shape.faces[i];
        const Vec3 a = shape.vertices[face.corners[0]];
        face.normal = normalized(cross(shape.vertices[face.corners[1]] - a, shape.vertices[face.corners[2]] - a));
    }
    // Every vertex of a regular solid is equally far from its centre.
    shape.circumradius = length(shape.vertices[0]);
}

} // namespace

DieShape makeD6(float edgeLength) {
    DieShape shape{};
    const float half = edgeLength / 2;
    shape.vertexCount = D6_VERTEX_COUNT;
    for (uint8_t i = 0; i < D6_VERTEX_COUNT; i++) {
        shape.vertices[i] = {signedHalf(i, X_BIT, half), signedHalf(i, Y_BIT, half), signedHalf(i, Z_BIT, half)};
    }
    shape.faceCount = D6_FACE_COUNT;
    for (uint8_t i = 0; i < D6_FACE_COUNT; i++) {
        const SquareFace& square = D6_FACES[i];
        DieFace& face = shape.faces[i];
        for (uint8_t corner = 0; corner < SQUARE_CORNERS; corner++) face.corners[corner] = square.corners[corner];
        face.cornerCount = SQUARE_CORNERS;
        face.numeral = square.numeral;
    }
    shape.inertiaPerMass = CUBE_INERTIA_PER_MASS_EDGE_SQUARED * edgeLength * edgeLength;
    finish(shape);
    return shape;
}

DieShape makeD8(float edgeLength) {
    DieShape shape{};
    // Neighbouring axis vertices at distance r are r·√2 apart.
    const float r = edgeLength / SQRT_2;
    shape.vertexCount = D8_VERTEX_COUNT;
    shape.vertices[PLUS_X] = {r, 0, 0};
    shape.vertices[PLUS_X + MINUS_OFFSET] = {-r, 0, 0};
    shape.vertices[PLUS_Y] = {0, r, 0};
    shape.vertices[PLUS_Y + MINUS_OFFSET] = {0, -r, 0};
    shape.vertices[PLUS_Z] = {0, 0, r};
    shape.vertices[PLUS_Z + MINUS_OFFSET] = {0, 0, -r};
    shape.faceCount = D8_FACE_COUNT;
    for (uint8_t i = 0; i < D8_FACE_COUNT; i++) shape.faces[i] = octantFace(D8_OCTANTS[i]);
    shape.inertiaPerMass = OCTAHEDRON_INERTIA_PER_MASS_EDGE_SQUARED * edgeLength * edgeLength;
    finish(shape);
    return shape;
}

DieShape makeD20(float edgeLength) {
    DieShape shape{};
    const float scale = edgeLength / D20_UNIT_EDGE_LENGTH;
    shape.vertexCount = D20_VERTEX_COUNT;
    for (uint8_t i = 0; i < HALF_VERTICES; i++) {
        shape.vertices[i] = HALF_OF_VERTICES[i] * scale;
        shape.vertices[i + HALF_VERTICES] = HALF_OF_VERTICES[i] * -scale;
    }
    shape.faceCount = D20_FACE_COUNT;
    for (uint8_t i = 0; i < HALF_FACES; i++) {
        const Triangle face = HALF_OF_FACES[i];
        const uint8_t numeral = i + 1;
        shape.faces[i] = triangleFace(face, numeral);
        // Mirroring through the centre reverses a triangle's winding, so two vertices swap
        // to keep the opposite face wound outward too.
        const Triangle opposite{oppositeVertex(face.a), oppositeVertex(face.c), oppositeVertex(face.b)};
        shape.faces[i + HALF_FACES] = triangleFace(opposite, D20_OPPOSITE_NUMERAL_SUM - numeral);
    }
    shape.inertiaPerMass = ICOSAHEDRON_INERTIA_PER_MASS_EDGE_SQUARED * edgeLength * edgeLength;
    finish(shape);
    return shape;
}

Vec3 faceCentre(const DieShape& shape, uint8_t face) {
    const DieFace& f = shape.faces[face];
    Vec3 sum{0, 0, 0};
    for (uint8_t corner = 0; corner < f.cornerCount; corner++) sum = sum + shape.vertices[f.corners[corner]];
    return sum * (1.0f / f.cornerCount);
}

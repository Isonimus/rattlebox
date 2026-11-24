#include "die_shape.h"

namespace {

constexpr float GOLDEN_RATIO = 1.6180339887f;
constexpr float SQRT_2 = 1.4142135624f;

// Each die's lock hold: the next hundredth of a second strictly above four times the longest
// pause after which that die still tipped onto another face, over 2,000 traced rolls
// (ADR-0028, scripts/fairness-probe.mjs). They live here, with the dice, because how long a body
// can balance on an edge before it goes over is a fact about the body, like its inertia; a
// single product-wide hold made every die wait out the worst of them.
//
// Re-measured at ADR-0031's tighter angular rest speed, which is why they are so much shorter
// than they were: most of what they used to wait out was a die still turning, not balancing.
// The D6's and the D8's traces showed no false rest at all, so 0.01 s is the rule's floor for
// them rather than a measured balance, and it is still 2.4 physics steps. Every other hold here,
// the D4's included, comes from a pause that die's own trace measured.
// 0.0042 s is the shortest pause of the dice that pause at all: the D12 and the D20 measured
// 0.0083 s and the D10 0.0167 s, while the D6 and the D8 showed none. Four times it rounds up to
// 0.02 s. ADR-0031 found that blunter dice linger longer near balance, and a tetrahedron is the
// sharpest die here at a 70.53 degree dihedral, but the traces do not order the dice by dihedral
// alone (the D8 is blunter than the D6 and still shows nothing), so this is the measurement and
// not a deduction from the shape.
constexpr float D4_HOLD_S = 0.02f;
constexpr float D6_HOLD_S = 0.01f;
constexpr float D8_HOLD_S = 0.01f;
constexpr float D10_HOLD_S = 0.07f;
constexpr float D12_HOLD_S = 0.04f;
constexpr float D20_HOLD_S = 0.04f;

// ---- D4

constexpr uint8_t D4_VERTEX_COUNT = 4;
constexpr uint8_t D4_FACE_COUNT = 4;
constexpr uint8_t D4_CORNERS = 3;
// The vertices below are alternate corners of a cube of side 2, which puts the centroid at the
// origin exactly rather than to rounding. Two of them differ in two coordinates by 2 each, so
// the edge is 2√2.
constexpr float D4_UNIT_EDGE_LENGTH = 2.0f * SQRT_2;
// A uniform regular tetrahedron of edge a: I = m a² / 20, the same about every axis through the
// centroid (scripts/tetrahedron-probe.mjs, agreeing with the integrated body to 2.8e-17).
constexpr float TETRAHEDRON_INERTIA_PER_MASS_EDGE_SQUARED = 1.0f / 20;

// Alternate corners of the cube: an even number of minus signs.
constexpr Vec3 D4_UNIT_VERTICES[D4_VERTEX_COUNT] = {{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}};

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

// ---- D10

constexpr uint8_t D10_VERTEX_COUNT = 12;
constexpr uint8_t D10_FACE_COUNT = 10;
constexpr uint8_t KITE_CORNERS = 4;
constexpr uint8_t D10_OPPOSITE_NUMERAL_SUM = D10_FACE_COUNT + 1;
// Two apexes on the z axis, then one zigzag ring of ten between them.
constexpr uint8_t D10_TOP_APEX = 0;
constexpr uint8_t D10_BOTTOM_APEX = 1;
constexpr uint8_t D10_FIRST_RING = 2;
constexpr uint8_t D10_RING_COUNT = 10;
constexpr uint8_t D10_FACES_PER_APEX = D10_RING_COUNT / 2;
// Named for this die rather than as a bare TWO_PI: a test that includes two source files merges
// their anonymous namespaces, and launch.cpp already has a turn of its own.
constexpr float D10_FULL_TURN_RAD = 6.2831853072f;
constexpr float D10_RING_STEP_RAD = D10_FULL_TURN_RAD / D10_RING_COUNT;

// A pentagonal trapezohedron's ring lies at this fraction of its apex height whatever its
// proportion: 1 − 2/√5, which scripts/die-shape-probe.mjs confirms across the whole family.
constexpr float TRAPEZOHEDRON_RING_Z_PER_CIRCUMRADIUS = 0.1055728090f;
// The one number that picks this D10 out of that family. A trapezohedron is not Platonic, so
// its inertia is a tensor in general and DieShape's single inertiaPerMass would be a lie the
// contact solver cannot correct. At this ring radius the tensor is isotropic, measured to
// 1.8e-18 on its off-diagonal terms, so the scalar is exact (slice 0027,
// scripts/die-shape-probe.mjs).
constexpr float D10_RING_RADIUS_PER_CIRCUMRADIUS = 0.8484859720f;
// And at that proportion, I = 0.2022291 m R² about every axis.
constexpr float D10_INERTIA_PER_MASS_CIRCUMRADIUS_SQUARED = 0.2022291236f;

/** @brief Ring vertex @p step, counted round from the first and wrapping. */
uint8_t ringVertex(uint8_t step) { return D10_FIRST_RING + step % D10_RING_COUNT; }

DieFace kiteFace(uint8_t apex, uint8_t near, uint8_t far, uint8_t alsoNear, uint8_t numeral) {
    return {{apex, near, far, alsoNear}, KITE_CORNERS, {}, numeral};
}

// ---- D12

constexpr uint8_t D12_VERTEX_COUNT = 20;
constexpr uint8_t D12_FACE_COUNT = 12;
constexpr uint8_t PENTAGON_CORNERS = 5;
constexpr uint8_t HALF_D12_VERTICES = D12_VERTEX_COUNT / 2;
constexpr uint8_t HALF_D12_FACES = D12_FACE_COUNT / 2;
constexpr uint8_t D12_OPPOSITE_NUMERAL_SUM = D12_FACE_COUNT + 1;
constexpr float INVERSE_GOLDEN_RATIO = 1.0f / GOLDEN_RATIO;
// The vertices below are a cube of side 2 with the three golden rectangles that meet it at its
// faces, and their pentagon edge is 2/φ.
constexpr float D12_UNIT_EDGE_LENGTH = 2.0f * INVERSE_GOLDEN_RATIO;
// A uniform regular dodecahedron of edge a: I = (39φ + 28) m a² / 150. Integrated from the solid
// and checked against this closed form by scripts/dodecahedron-probe.mjs, agreeing to 5.6e-17.
// Being Platonic it is the same about every axis, so DieShape's single scalar is exact for it
// without a proportion having to be chosen for it as the D10's was (slice 0029).
constexpr float DODECAHEDRON_INERTIA_PER_MASS_EDGE_SQUARED = (39 * GOLDEN_RATIO + 28) / 150;

// Vertex i + 10 is vertex i through the centre, so only half are listed: the cube's four corners
// that are not each other's opposites, then one corner of each golden rectangle's two ends.
constexpr Vec3 HALF_OF_D12_VERTICES[HALF_D12_VERTICES] = {
    {1, 1, 1}, {1, 1, -1}, {1, -1, 1}, {-1, 1, 1},
    {0, INVERSE_GOLDEN_RATIO, GOLDEN_RATIO}, {0, -INVERSE_GOLDEN_RATIO, GOLDEN_RATIO},
    {INVERSE_GOLDEN_RATIO, GOLDEN_RATIO, 0}, {-INVERSE_GOLDEN_RATIO, GOLDEN_RATIO, 0},
    {GOLDEN_RATIO, 0, INVERSE_GOLDEN_RATIO}, {GOLDEN_RATIO, 0, -INVERSE_GOLDEN_RATIO},
};

struct Pentagon {
    uint8_t corners[PENTAGON_CORNERS];
};

// Face i + 6 is face i through the centre, so only half are listed: one of each pair of opposite
// faces, wound outward.
//
// These indices were not written out by hand: a face list that is wrong still looks like a solid
// until the inertia or a numeral comes out wrong. They came from running
// scripts/dodecahedron-probe.mjs's hullFaces over the vertex order above, which is not the order
// that probe's own dodecahedron() builds, so its indices are not interchangeable with these. What
// makes the table trustworthy is not that provenance but test_geometry, which checks this solid
// closes, winds outward, has one edge length and one inradius, and has an inertia matching the
// body integrated about all three axes.
constexpr Pentagon HALF_OF_D12_FACES[HALF_D12_FACES] = {
    {{0, 8, 9, 1, 6}}, {{0, 4, 5, 2, 8}},
    {{0, 6, 7, 3, 4}}, {{1, 15, 12, 7, 6}},
    {{1, 9, 13, 14, 15}}, {{2, 17, 13, 9, 8}},
};

uint8_t oppositeD12Vertex(uint8_t vertex) { return (vertex + HALF_D12_VERTICES) % D12_VERTEX_COUNT; }

DieFace pentagonFace(const Pentagon& pentagon, uint8_t numeral) {
    DieFace face{};
    for (uint8_t corner = 0; corner < PENTAGON_CORNERS; corner++) face.corners[corner] = pentagon.corners[corner];
    face.cornerCount = PENTAGON_CORNERS;
    face.numeral = numeral;
    return face;
}

// ---- shared

/** @brief Fill in what follows from the vertices and faces: normals and the bounding radius. */
void finish(DieShape& shape) {
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        DieFace& face = shape.faces[i];
        const Vec3 a = shape.vertices[face.corners[0]];
        face.normal = normalized(cross(shape.vertices[face.corners[1]] - a, shape.vertices[face.corners[2]] - a));
    }
    // The furthest vertex, not simply the first: a D10's apexes reach further than its ring, so
    // taking vertex 0 would hand the dice-to-dice test a sphere some of the die pokes out of.
    shape.circumradius = 0;
    for (uint8_t i = 0; i < shape.vertexCount; i++) {
        const float reach = length(shape.vertices[i]);
        if (reach > shape.circumradius) shape.circumradius = reach;
    }
}

} // namespace

DieShape makeD4(float edgeLength) {
    DieShape shape{};
    const float scale = edgeLength / D4_UNIT_EDGE_LENGTH;
    shape.vertexCount = D4_VERTEX_COUNT;
    for (uint8_t i = 0; i < D4_VERTEX_COUNT; i++) shape.vertices[i] = D4_UNIT_VERTICES[i] * scale;

    shape.faceCount = D4_FACE_COUNT;
    for (uint8_t i = 0; i < D4_FACE_COUNT; i++) {
        // Face i is the one opposite vertex i, which is the three other vertices. That pairing is
        // what the apex reading rule names (ADR-0033): the die rests on face i and raises vertex
        // i, and the probe measures the two to be exactly antipodal.
        DieFace& face = shape.faces[i];
        uint8_t next = 0;
        for (uint8_t corner = 0; corner < D4_VERTEX_COUNT; corner++) {
            if (corner != i) face.corners[next++] = corner;
        }
        face.cornerCount = D4_CORNERS;
        // Ascending index order winds a face either way depending on which vertex was left out,
        // so the winding is fixed against the one direction that is known: outward is away from
        // the vertex this face is opposite. Swapping two corners reverses it.
        const Vec3 a = shape.vertices[face.corners[0]];
        const Vec3 raw = cross(shape.vertices[face.corners[1]] - a, shape.vertices[face.corners[2]] - a);
        if (dot(raw, shape.vertices[i]) > 0) {
            const uint8_t swapped = face.corners[1];
            face.corners[1] = face.corners[2];
            face.corners[2] = swapped;
        }
        // 1 to 4, with no opposite-face sum to honour: a tetrahedron has no opposite face.
        face.numeral = i + 1;
    }

    shape.inertiaPerMass = TETRAHEDRON_INERTIA_PER_MASS_EDGE_SQUARED * edgeLength * edgeLength;
    shape.holdS = D4_HOLD_S;
    shape.reading = NumeralReading::Apex;
    finish(shape);
    return shape;
}

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
    shape.holdS = D6_HOLD_S;
    shape.reading = NumeralReading::TopFace;
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
    shape.holdS = D8_HOLD_S;
    shape.reading = NumeralReading::TopFace;
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
    shape.holdS = D20_HOLD_S;
    shape.reading = NumeralReading::TopFace;
    finish(shape);
    return shape;
}

DieShape makeD10(float circumradius) {
    DieShape shape{};
    shape.vertexCount = D10_VERTEX_COUNT;
    shape.vertices[D10_TOP_APEX] = {0, 0, circumradius};
    shape.vertices[D10_BOTTOM_APEX] = {0, 0, -circumradius};
    const float ringRadius = circumradius * D10_RING_RADIUS_PER_CIRCUMRADIUS;
    const float ringZ = circumradius * TRAPEZOHEDRON_RING_Z_PER_CIRCUMRADIUS;
    for (uint8_t step = 0; step < D10_RING_COUNT; step++) {
        const float angle = D10_RING_STEP_RAD * (step + 1);
        // The ring zigzags: the even steps are the corners the top apex's kites reach down to,
        // the odd ones the corners the bottom apex's kites reach up to.
        const float z = (step % 2 == 0) ? ringZ : -ringZ;
        shape.vertices[ringVertex(step)] = {ringRadius * cosf(angle), ringRadius * sinf(angle), z};
    }

    shape.faceCount = D10_FACE_COUNT;
    for (uint8_t i = 0; i < D10_FACES_PER_APEX; i++) {
        const uint8_t step = 2 * i;
        const uint8_t numeral = i + 1;
        shape.faces[i] = kiteFace(D10_TOP_APEX, ringVertex(step), ringVertex(step + 1), ringVertex(step + 2), numeral);
        // The solid is centrally symmetric, so the opposite face is this one's corners through
        // the centre: a ring vertex five steps round, and the other apex. Mirroring reverses the
        // winding, so the corners are laid down the other way to keep it outward.
        shape.faces[i + D10_FACES_PER_APEX] =
            kiteFace(D10_BOTTOM_APEX, ringVertex(step + 7), ringVertex(step + 6), ringVertex(step + 5),
                     D10_OPPOSITE_NUMERAL_SUM - numeral);
    }
    shape.inertiaPerMass = D10_INERTIA_PER_MASS_CIRCUMRADIUS_SQUARED * circumradius * circumradius;
    shape.holdS = D10_HOLD_S;
    shape.reading = NumeralReading::TopFace;
    finish(shape);
    return shape;
}

DieShape makeD12(float edgeLength) {
    DieShape shape{};
    const float scale = edgeLength / D12_UNIT_EDGE_LENGTH;
    shape.vertexCount = D12_VERTEX_COUNT;
    for (uint8_t i = 0; i < HALF_D12_VERTICES; i++) {
        shape.vertices[i] = HALF_OF_D12_VERTICES[i] * scale;
        shape.vertices[i + HALF_D12_VERTICES] = HALF_OF_D12_VERTICES[i] * -scale;
    }
    shape.faceCount = D12_FACE_COUNT;
    for (uint8_t i = 0; i < HALF_D12_FACES; i++) {
        const Pentagon& pentagon = HALF_OF_D12_FACES[i];
        const uint8_t numeral = i + 1;
        shape.faces[i] = pentagonFace(pentagon, numeral);
        // Mirroring through the centre reverses a pentagon's winding, so its corners are laid
        // down the other way round to keep the opposite face wound outward too.
        Pentagon opposite{};
        for (uint8_t corner = 0; corner < PENTAGON_CORNERS; corner++) {
            opposite.corners[corner] = oppositeD12Vertex(pentagon.corners[PENTAGON_CORNERS - 1 - corner]);
        }
        shape.faces[i + HALF_D12_FACES] = pentagonFace(opposite, D12_OPPOSITE_NUMERAL_SUM - numeral);
    }
    shape.inertiaPerMass = DODECAHEDRON_INERTIA_PER_MASS_EDGE_SQUARED * edgeLength * edgeLength;
    shape.holdS = D12_HOLD_S;
    shape.reading = NumeralReading::TopFace;
    finish(shape);
    return shape;
}

Vec3 faceCentre(const DieShape& shape, uint8_t face) {
    const DieFace& f = shape.faces[face];
    Vec3 sum{0, 0, 0};
    for (uint8_t corner = 0; corner < f.cornerCount; corner++) sum = sum + shape.vertices[f.corners[corner]];
    return sum * (1.0f / f.cornerCount);
}

#include <unity.h>
#include "../../src/geometry/rotation.h"
#include "../../src/geometry/rotation.cpp"
#include "../../src/geometry/die_shape.h"
#include "../../src/geometry/die_shape.cpp"
#include "../../src/roll/die_catalog.h"
#include "../../src/roll/die_catalog.cpp"

// The shapes are checked by properties every regular solid has, not against a copy of their
// own tables, so a wrong index cannot agree with itself.

static const float EDGE = 12.0f;
static const float TOLERANCE = 1e-4f;
static const uint8_t FACES_PER_EDGE = 2;
// Euler's formula for a closed solid with no holes: V − E + F = 2.
static const int EULER_CHARACTERISTIC = 2;
static const float QUARTER_TURN_RAD = 1.5707963f;
static const uint8_t AXIS_X = 0;
static const uint8_t AXIS_Y = 1;
static const uint8_t AXIS_COUNT = 3;

struct Expected {
    const char* name;
    uint8_t     vertices;
    uint8_t     faces;
    uint8_t     cornersPerFace;
};

static const Expected D4{"D4", 4, 4, 3};
static const Expected D6{"D6", 8, 6, 4};
static const Expected D8{"D8", 6, 8, 3};
static const Expected D10{"D10", 12, 10, 4};
static const Expected D12{"D12", 20, 12, 5};
static const Expected D20{"D20", 12, 20, 3};

static void assertVecEqual(Vec3 expected, Vec3 actual) {
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, expected.x, actual.x);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, expected.y, actual.y);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, expected.z, actual.z);
}

static void assertCounts(const DieShape& shape, const Expected& expected) {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected.vertices, shape.vertexCount, expected.name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected.faces, shape.faceCount, expected.name);
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected.cornersPerFace, shape.faces[i].cornerCount, expected.name);
    }
}

static void assertEveryEdgeHasTheRequestedLength(const DieShape& shape) {
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        const DieFace& face = shape.faces[i];
        for (uint8_t corner = 0; corner < face.cornerCount; corner++) {
            const Vec3 from = shape.vertices[face.corners[corner]];
            const Vec3 to = shape.vertices[face.corners[(corner + 1) % face.cornerCount]];
            TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, EDGE, length(to - from));
        }
    }
}

static void assertTheFacesCloseTheBody(const DieShape& shape) {
    // Every edge joins exactly two faces, and Euler's formula holds: no hole, no duplicate.
    uint8_t facesAtEdge[DIE_MAX_VERTICES][DIE_MAX_VERTICES] = {};
    int edgeCount = 0;
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        const DieFace& face = shape.faces[i];
        for (uint8_t corner = 0; corner < face.cornerCount; corner++) {
            const uint8_t from = face.corners[corner];
            const uint8_t to = face.corners[(corner + 1) % face.cornerCount];
            uint8_t& count = facesAtEdge[from < to ? from : to][from < to ? to : from];
            if (count == 0) edgeCount++;
            count++;
        }
    }
    for (uint8_t a = 0; a < shape.vertexCount; a++) {
        for (uint8_t b = a + 1; b < shape.vertexCount; b++) {
            const uint8_t count = facesAtEdge[a][b];
            TEST_ASSERT_TRUE(count == 0 || count == FACES_PER_EDGE);
        }
    }
    TEST_ASSERT_EQUAL_INT(EULER_CHARACTERISTIC, shape.vertexCount - edgeCount + shape.faceCount);
}

static void assertEveryFaceIsFlatWindsOutwardAndCarriesItsUnitNormal(const DieShape& shape) {
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        const DieFace& face = shape.faces[i];
        const Vec3 centre = faceCentre(shape, i);
        for (uint8_t corner = 0; corner < face.cornerCount; corner++) {
            const Vec3 here = shape.vertices[face.corners[corner]];
            const Vec3 next = shape.vertices[face.corners[(corner + 1) % face.cornerCount]];
            const Vec3 windingNormal = cross(here - centre, next - centre);
            TEST_ASSERT_TRUE_MESSAGE(dot(windingNormal, centre) > 0, "face winds inward");
            assertVecEqual(normalized(windingNormal), face.normal);
        }
    }
}

static void assertNumeralsRunOneToN(const DieShape& shape) {
    bool seen[DIE_MAX_FACES + 1] = {};
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        const uint8_t numeral = shape.faces[i].numeral;
        TEST_ASSERT_TRUE(numeral >= 1 && numeral <= shape.faceCount);
        TEST_ASSERT_FALSE(seen[numeral]);
        seen[numeral] = true;
    }
}

/** @brief How many of @p shape's faces point exactly opposite face @p face. */
static uint8_t facesOpposite(const DieShape& shape, uint8_t face) {
    uint8_t opposites = 0;
    for (uint8_t j = 0; j < shape.faceCount; j++) {
        if (dot(shape.faces[face].normal, shape.faces[j].normal) < -1.0f + TOLERANCE) opposites++;
    }
    return opposites;
}

static void assertNumeralsRunOneToNAndOppositeFacesSumToNPlusOne(const DieShape& shape) {
    assertNumeralsRunOneToN(shape);
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        const uint8_t numeral = shape.faces[i].numeral;
        for (uint8_t j = 0; j < shape.faceCount; j++) {
            if (dot(shape.faces[i].normal, shape.faces[j].normal) < -1.0f + TOLERANCE) {
                TEST_ASSERT_EQUAL_UINT8(shape.faceCount + 1, numeral + shape.faces[j].numeral);
            }
        }
        TEST_ASSERT_EQUAL_UINT8(1, facesOpposite(shape, i));
    }
}

/**
 * @brief Invariant #7's TopFace half: every face has exactly one exactly opposed to it.
 *
 * What makes "the face toward the glass" a complete reading rule. A die declaring TopFace is
 * claiming this about its own solid (ADR-0033), so the claim is checked rather than trusted.
 */
static void assertEveryFaceIsOppositeOneFace(const DieShape& shape) {
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, facesOpposite(shape, i), "a TopFace die has a face with no opposite");
    }
}

/**
 * @brief Invariant #7's Apex half: every vertex points exactly opposite one face, and belongs to
 * none of that face's corners.
 *
 * What makes "the face it rests on, read at the apex above it" well defined, and what lets the
 * renderer find the apex as the one vertex the resting face does not touch (ADR-0033).
 */
static void assertEveryVertexIsOppositeOneFace(const DieShape& shape) {
    for (uint8_t v = 0; v < shape.vertexCount; v++) {
        const Vec3 direction = normalized(shape.vertices[v]);
        uint8_t opposites = 0;
        for (uint8_t i = 0; i < shape.faceCount; i++) {
            if (dot(direction, shape.faces[i].normal) > -1.0f + TOLERANCE) continue;
            opposites++;
            for (uint8_t corner = 0; corner < shape.faces[i].cornerCount; corner++) {
                TEST_ASSERT_NOT_EQUAL_MESSAGE(v, shape.faces[i].corners[corner], "an apex touches its own resting face");
            }
        }
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, opposites, "an Apex die has a vertex with no opposite face");
    }
}

static float coordinate(Vec3 v, uint8_t axis) { return axis == AXIS_X ? v.x : (axis == AXIS_Y ? v.y : v.z); }

/**
 * @brief The solid's moment of inertia about @p axis per unit mass, integrated from its faces:
 * each face fans into tetrahedra from the centre, and a tetrahedron (0, a, b, c) of volume V has
 * ∫x² dV = V/10 (a² + b² + c² + ab + ac + bc) in each coordinate.
 */
static float integratedInertiaPerMassAbout(const DieShape& shape, uint8_t axis) {
    float volume = 0;
    float secondMoment = 0;
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        const DieFace& face = shape.faces[i];
        const Vec3 a = shape.vertices[face.corners[0]];
        for (uint8_t corner = 1; corner + 1 < face.cornerCount; corner++) {
            const Vec3 b = shape.vertices[face.corners[corner]];
            const Vec3 c = shape.vertices[face.corners[corner + 1]];
            const float tetraVolume = dot(a, cross(b, c)) / 6;
            volume += tetraVolume;
            // The moment about one axis sums the second moments of the other two coordinates.
            for (uint8_t other = AXIS_X; other < AXIS_COUNT; other++) {
                if (other == axis) continue;
                const float p = coordinate(a, other), q = coordinate(b, other), r = coordinate(c, other);
                secondMoment += tetraVolume / 10 * (p * p + q * q + r * r + p * q + p * r + q * r);
            }
        }
    }
    return secondMoment / volume;
}

/**
 * @brief Every axis, not just one: DieShape carries a single inertia for all three, which for a
 * die that is not a Platonic solid is a claim about the shape and not a property of its class.
 */
static void assertInertiaMatchesTheIntegratedBody(const DieShape& shape) {
    for (uint8_t axis = AXIS_X; axis < AXIS_COUNT; axis++) {
        const float integrated = integratedInertiaPerMassAbout(shape, axis);
        TEST_ASSERT_FLOAT_WITHIN(integrated * TOLERANCE, integrated, shape.inertiaPerMass);
    }
}

static void assertTheCircumradiusReachesEveryVertex(const DieShape& shape) {
    for (uint8_t i = 0; i < shape.vertexCount; i++) {
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, shape.circumradius, length(shape.vertices[i]));
    }
}

/** @brief The bounding sphere holds the whole die, and is not larger than it needs to be. */
static void assertTheCircumradiusIsTheFurthestVertex(const DieShape& shape) {
    float furthest = 0;
    for (uint8_t i = 0; i < shape.vertexCount; i++) {
        const float reach = length(shape.vertices[i]);
        TEST_ASSERT_TRUE_MESSAGE(reach <= shape.circumradius + TOLERANCE, "a vertex escapes the bounding sphere");
        if (reach > furthest) furthest = reach;
    }
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, furthest, shape.circumradius);
}

/**
 * @brief Every face the same distance from the centre.
 *
 * This is what makes a die that is not regular still fair: it rests on any face the same way,
 * so no face is geometrically privileged (slice 0027).
 */
static void assertEveryFaceIsTheSameDistanceFromTheCentre(const DieShape& shape) {
    const float inradius = dot(shape.faces[0].normal, faceCentre(shape, 0));
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, inradius, dot(shape.faces[i].normal, faceCentre(shape, i)));
    }
}

static void assertRegularSolid(const DieShape& shape, const Expected& expected) {
    assertCounts(shape, expected);
    assertEveryEdgeHasTheRequestedLength(shape);
    assertTheFacesCloseTheBody(shape);
    assertEveryFaceIsFlatWindsOutwardAndCarriesItsUnitNormal(shape);
    assertNumeralsRunOneToNAndOppositeFacesSumToNPlusOne(shape);
    assertInertiaMatchesTheIntegratedBody(shape);
    assertTheCircumradiusReachesEveryVertex(shape);
    assertEveryFaceIsTheSameDistanceFromTheCentre(shape);
}

/**
 * @brief A die whose faces are all alike but whose edges and vertices are not.
 *
 * The D10 is one: its faces are kites, so it has two edge lengths, and its apexes reach further
 * than its ring. What a die actually needs is that every face is alike, which is checked here;
 * the regular dice keep the stricter assertion above.
 */
static void assertIsohedralSolid(const DieShape& shape, const Expected& expected) {
    assertCounts(shape, expected);
    assertTheFacesCloseTheBody(shape);
    assertEveryFaceIsFlatWindsOutwardAndCarriesItsUnitNormal(shape);
    assertNumeralsRunOneToNAndOppositeFacesSumToNPlusOne(shape);
    assertInertiaMatchesTheIntegratedBody(shape);
    assertTheCircumradiusIsTheFurthestVertex(shape);
    assertEveryFaceIsTheSameDistanceFromTheCentre(shape);
}

/**
 * @brief A regular solid with no opposite face at all: the tetrahedron.
 *
 * Everything assertRegularSolid checks except the opposite-face numbering, which a tetrahedron
 * cannot have, because no two of its faces are parallel (ADR-0033).
 */
static void assertRegularSolidWithoutOpposites(const DieShape& shape, const Expected& expected) {
    assertCounts(shape, expected);
    assertEveryEdgeHasTheRequestedLength(shape);
    assertTheFacesCloseTheBody(shape);
    assertEveryFaceIsFlatWindsOutwardAndCarriesItsUnitNormal(shape);
    assertNumeralsRunOneToN(shape);
    assertInertiaMatchesTheIntegratedBody(shape);
    assertTheCircumradiusReachesEveryVertex(shape);
    assertEveryFaceIsTheSameDistanceFromTheCentre(shape);
}

void test_the_d4_is_a_closed_outward_tetrahedron_numbered_one_to_four() {
    assertRegularSolidWithoutOpposites(makeD4(EDGE), D4);
}

void test_no_two_of_the_d4s_faces_are_opposed_which_is_why_it_needs_its_own_reading_rule() {
    const DieShape d4 = makeD4(EDGE);
    for (uint8_t i = 0; i < d4.faceCount; i++) TEST_ASSERT_EQUAL_UINT8(0, facesOpposite(d4, i));
    // What it has instead: every vertex exactly opposite a face it does not touch.
    assertEveryVertexIsOppositeOneFace(d4);
}

void test_the_d4s_inertia_is_an_edge_squared_over_twenty_the_tetrahedrons_closed_form() {
    // An independent route to the same number as the integrated body above: m a² / 20.
    TEST_ASSERT_FLOAT_WITHIN(EDGE * EDGE * TOLERANCE, EDGE * EDGE / 20.0f, makeD4(EDGE).inertiaPerMass);
}

void test_every_catalog_die_declares_a_reading_rule_its_own_geometry_supports() {
    // Standing invariant #7. A declaration is a claim about the solid, so it is checked against
    // the solid rather than against a list of which die is which (ADR-0033).
    for (uint8_t type = 0; type < DIE_TYPE_COUNT; type++) {
        const DieKind& kind = dieKind(static_cast<DieType>(type));
        if (kind.shape.reading == NumeralReading::Apex) {
            assertEveryVertexIsOppositeOneFace(kind.shape);
        } else {
            assertEveryFaceIsOppositeOneFace(kind.shape);
        }
    }
}

void test_the_d6_is_a_closed_outward_cube_with_opposites_summing_to_7() {
    assertRegularSolid(makeD6(EDGE), D6);
}

void test_the_d8_is_a_closed_outward_octahedron_with_opposites_summing_to_9() {
    assertRegularSolid(makeD8(EDGE), D8);
}

void test_the_d10_is_a_closed_outward_trapezohedron_with_opposites_summing_to_11() {
    assertIsohedralSolid(makeD10(EDGE), D10);
}

void test_the_d10_has_two_edge_lengths_in_adjacent_pairs_so_each_face_is_a_kite() {
    const DieShape d10 = makeD10(EDGE);
    for (uint8_t i = 0; i < d10.faceCount; i++) {
        const DieFace& face = d10.faces[i];
        float side[FACE_MAX_CORNERS];
        for (uint8_t corner = 0; corner < face.cornerCount; corner++) {
            side[corner] = length(d10.vertices[face.corners[(corner + 1) % face.cornerCount]] -
                                  d10.vertices[face.corners[corner]]);
        }
        // A kite's two equal pairs are adjacent, not opposite; which pair the corner list starts
        // on depends on the winding, so either split is a kite and an opposite split is not.
        const bool firstSplit = fabsf(side[0] - side[1]) < TOLERANCE && fabsf(side[2] - side[3]) < TOLERANCE;
        const bool secondSplit = fabsf(side[1] - side[2]) < TOLERANCE && fabsf(side[3] - side[0]) < TOLERANCE;
        TEST_ASSERT_TRUE_MESSAGE(firstSplit || secondSplit, "a D10 face is not a kite");
    }
}

void test_the_d10s_apexes_reach_further_than_its_ring_so_its_circumradius_is_not_every_vertex() {
    const DieShape d10 = makeD10(EDGE);
    // The apexes sit on the z axis at the circumradius; every other vertex is strictly inside it.
    uint8_t atTheCircumradius = 0;
    for (uint8_t i = 0; i < d10.vertexCount; i++) {
        if (fabsf(length(d10.vertices[i]) - d10.circumradius) < TOLERANCE) atTheCircumradius++;
    }
    TEST_ASSERT_EQUAL_UINT8(2, atTheCircumradius);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, EDGE, d10.circumradius);
}

void test_the_d12_is_a_closed_outward_dodecahedron_with_opposites_summing_to_13() {
    assertRegularSolid(makeD12(EDGE), D12);
}

void test_the_d20_is_a_closed_outward_icosahedron_with_opposites_summing_to_21() {
    assertRegularSolid(makeD20(EDGE), D20);
}

void test_a_face_centre_is_the_mean_of_its_corners() {
    // The +z face of a cube of edge 12 is centred 6 above the cube's centre.
    const DieShape d6 = makeD6(EDGE);
    for (uint8_t i = 0; i < d6.faceCount; i++) assertVecEqual(d6.faces[i].normal * (EDGE / 2), faceCentre(d6, i));
}

void test_a_quarter_turn_about_z_takes_x_to_y() {
    const Rotation turn = rotationAbout({0, 0, 1}, QUARTER_TURN_RAD);
    assertVecEqual({0, 1, 0}, rotate(turn, {1, 0, 0}));
    assertVecEqual({-1, 0, 0}, rotate(turn, {0, 1, 0}));
    assertVecEqual({0, 0, 1}, rotate(turn, {0, 0, 1}));
}

void test_a_rotation_keeps_lengths_whatever_its_axis_length() {
    const Rotation turn = rotationAbout({3, -4, 12}, 1.0f);
    const Vec3 v{2, 7, -1};
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, length(v), length(rotate(turn, v)));
    // The axis itself does not move.
    assertVecEqual({3, -4, 12}, rotate(turn, {3, -4, 12}));
}

void test_the_identity_moves_nothing() {
    assertVecEqual({2, -3, 5}, rotate(IDENTITY_ROTATION, {2, -3, 5}));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_d4_is_a_closed_outward_tetrahedron_numbered_one_to_four);
    RUN_TEST(test_no_two_of_the_d4s_faces_are_opposed_which_is_why_it_needs_its_own_reading_rule);
    RUN_TEST(test_the_d4s_inertia_is_an_edge_squared_over_twenty_the_tetrahedrons_closed_form);
    RUN_TEST(test_every_catalog_die_declares_a_reading_rule_its_own_geometry_supports);
    RUN_TEST(test_the_d6_is_a_closed_outward_cube_with_opposites_summing_to_7);
    RUN_TEST(test_the_d8_is_a_closed_outward_octahedron_with_opposites_summing_to_9);
    RUN_TEST(test_the_d10_is_a_closed_outward_trapezohedron_with_opposites_summing_to_11);
    RUN_TEST(test_the_d10_has_two_edge_lengths_in_adjacent_pairs_so_each_face_is_a_kite);
    RUN_TEST(test_the_d10s_apexes_reach_further_than_its_ring_so_its_circumradius_is_not_every_vertex);
    RUN_TEST(test_the_d12_is_a_closed_outward_dodecahedron_with_opposites_summing_to_13);
    RUN_TEST(test_the_d20_is_a_closed_outward_icosahedron_with_opposites_summing_to_21);
    RUN_TEST(test_a_face_centre_is_the_mean_of_its_corners);
    RUN_TEST(test_a_quarter_turn_about_z_takes_x_to_y);
    RUN_TEST(test_a_rotation_keeps_lengths_whatever_its_axis_length);
    RUN_TEST(test_the_identity_moves_nothing);
    return UNITY_END();
}

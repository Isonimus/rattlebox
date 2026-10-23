#include <unity.h>
#include "../../src/geometry/rotation.h"
#include "../../src/geometry/rotation.cpp"
#include "../../src/geometry/die_shape.h"
#include "../../src/geometry/die_shape.cpp"

// The shapes are checked by properties every regular solid has, not against a copy of their
// own tables, so a wrong index cannot agree with itself.

static const float EDGE = 12.0f;
static const float TOLERANCE = 1e-4f;
static const uint8_t FACES_PER_EDGE = 2;
// Euler's formula for a closed solid with no holes: V − E + F = 2.
static const int EULER_CHARACTERISTIC = 2;
static const float QUARTER_TURN_RAD = 1.5707963f;

struct Expected {
    const char* name;
    uint8_t     vertices;
    uint8_t     faces;
    uint8_t     cornersPerFace;
};

static const Expected D6{"D6", 8, 6, 4};
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

static void assertNumeralsRunOneToNAndOppositeFacesSumToNPlusOne(const DieShape& shape) {
    bool seen[DIE_MAX_FACES + 1] = {};
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        const uint8_t numeral = shape.faces[i].numeral;
        TEST_ASSERT_TRUE(numeral >= 1 && numeral <= shape.faceCount);
        TEST_ASSERT_FALSE(seen[numeral]);
        seen[numeral] = true;
        uint8_t opposites = 0;
        for (uint8_t j = 0; j < shape.faceCount; j++) {
            if (dot(shape.faces[i].normal, shape.faces[j].normal) < -1.0f + TOLERANCE) {
                opposites++;
                TEST_ASSERT_EQUAL_UINT8(shape.faceCount + 1, numeral + shape.faces[j].numeral);
            }
        }
        TEST_ASSERT_EQUAL_UINT8(1, opposites);
    }
}

/**
 * @brief The solid's moment of inertia about z per unit mass, integrated from its faces: each
 * face fans into tetrahedra from the centre, and a tetrahedron (0, a, b, c) of volume V has
 * ∫x² dV = V/10 (a² + b² + c² + ab + ac + bc) in each coordinate.
 */
static float integratedInertiaPerMassAboutZ(const DieShape& shape) {
    float volume = 0;
    float secondMomentXY = 0;
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        const DieFace& face = shape.faces[i];
        const Vec3 a = shape.vertices[face.corners[0]];
        for (uint8_t corner = 1; corner + 1 < face.cornerCount; corner++) {
            const Vec3 b = shape.vertices[face.corners[corner]];
            const Vec3 c = shape.vertices[face.corners[corner + 1]];
            const float tetraVolume = dot(a, cross(b, c)) / 6;
            auto moment = [&](float p, float q, float r) {
                return tetraVolume / 10 * (p * p + q * q + r * r + p * q + p * r + q * r);
            };
            volume += tetraVolume;
            secondMomentXY += moment(a.x, b.x, c.x) + moment(a.y, b.y, c.y);
        }
    }
    return secondMomentXY / volume;
}

static void assertInertiaMatchesTheIntegratedBody(const DieShape& shape) {
    const float integrated = integratedInertiaPerMassAboutZ(shape);
    TEST_ASSERT_FLOAT_WITHIN(integrated * TOLERANCE, integrated, shape.inertiaPerMass);
}

static void assertTheCircumradiusReachesEveryVertex(const DieShape& shape) {
    for (uint8_t i = 0; i < shape.vertexCount; i++) {
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, shape.circumradius, length(shape.vertices[i]));
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
}

void test_the_d6_is_a_closed_outward_cube_with_opposites_summing_to_7() {
    assertRegularSolid(makeD6(EDGE), D6);
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
    RUN_TEST(test_the_d6_is_a_closed_outward_cube_with_opposites_summing_to_7);
    RUN_TEST(test_the_d20_is_a_closed_outward_icosahedron_with_opposites_summing_to_21);
    RUN_TEST(test_a_face_centre_is_the_mean_of_its_corners);
    RUN_TEST(test_a_quarter_turn_about_z_takes_x_to_y);
    RUN_TEST(test_a_rotation_keeps_lengths_whatever_its_axis_length);
    RUN_TEST(test_the_identity_moves_nothing);
    return UNITY_END();
}

#include <unity.h>
#include "../../src/geometry/rotation.h"
#include "../../src/geometry/rotation.cpp"
#include "../../src/geometry/d20_mesh.h"
#include "../../src/geometry/d20_mesh.cpp"

// The mesh is checked by properties every regular icosahedron has, not against a copy of
// its own tables, so a wrong index cannot agree with itself.

static const float EDGE_PX = 12.0f;
static const float TOLERANCE = 1e-4f;
static const uint8_t FACES_PER_VERTEX = 5;
static const uint8_t FACES_PER_EDGE = 2;
static const uint8_t OPPOSITE_SUM = 21;
static const float QUARTER_TURN_RAD = 1.5707963f;

static void assertVecEqual(Vec3 expected, Vec3 actual) {
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, expected.x, actual.x);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, expected.y, actual.y);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, expected.z, actual.z);
}

static Vec3 centroid(const D20Mesh& mesh, uint8_t face) {
    const FaceVertices f = mesh.faces[face];
    return (mesh.vertices[f.a] + mesh.vertices[f.b] + mesh.vertices[f.c]) * (1.0f / 3);
}

void test_every_edge_has_the_requested_length() {
    const D20Mesh mesh = makeD20(EDGE_PX);
    for (const FaceVertices& face : mesh.faces) {
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, EDGE_PX, length(mesh.vertices[face.a] - mesh.vertices[face.b]));
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, EDGE_PX, length(mesh.vertices[face.b] - mesh.vertices[face.c]));
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, EDGE_PX, length(mesh.vertices[face.c] - mesh.vertices[face.a]));
    }
}

void test_the_faces_close_the_body() {
    // Every vertex meets five faces, and every edge joins exactly two: no hole, no duplicate.
    const D20Mesh mesh = makeD20(EDGE_PX);
    uint8_t facesAtVertex[D20_VERTEX_COUNT] = {};
    uint8_t facesAtEdge[D20_VERTEX_COUNT][D20_VERTEX_COUNT] = {};
    for (const FaceVertices& face : mesh.faces) {
        const uint8_t corners[3] = {face.a, face.b, face.c};
        for (int i = 0; i < 3; i++) {
            const uint8_t from = corners[i];
            const uint8_t to = corners[(i + 1) % 3];
            facesAtVertex[from]++;
            facesAtEdge[from < to ? from : to][from < to ? to : from]++;
        }
    }
    for (uint8_t v = 0; v < D20_VERTEX_COUNT; v++) TEST_ASSERT_EQUAL_UINT8(FACES_PER_VERTEX, facesAtVertex[v]);
    for (uint8_t a = 0; a < D20_VERTEX_COUNT; a++) {
        for (uint8_t b = a + 1; b < D20_VERTEX_COUNT; b++) {
            const uint8_t count = facesAtEdge[a][b];
            TEST_ASSERT_TRUE(count == 0 || count == FACES_PER_EDGE);
        }
    }
}

void test_every_face_winds_outward_and_carries_its_unit_normal() {
    const D20Mesh mesh = makeD20(EDGE_PX);
    for (uint8_t i = 0; i < D20_FACE_COUNT; i++) {
        const FaceVertices f = mesh.faces[i];
        const Vec3 a = mesh.vertices[f.a];
        const Vec3 windingNormal = cross(mesh.vertices[f.b] - a, mesh.vertices[f.c] - a);
        TEST_ASSERT_TRUE_MESSAGE(dot(windingNormal, centroid(mesh, i)) > 0, "face winds inward");
        assertVecEqual(normalized(windingNormal), mesh.normals[i]);
    }
}

void test_numerals_run_1_to_20_and_opposite_faces_sum_to_21() {
    const D20Mesh mesh = makeD20(EDGE_PX);
    bool seen[D20_FACE_COUNT + 1] = {};
    for (uint8_t i = 0; i < D20_FACE_COUNT; i++) {
        const uint8_t numeral = mesh.numerals[i];
        TEST_ASSERT_TRUE(numeral >= 1 && numeral <= D20_FACE_COUNT);
        TEST_ASSERT_FALSE(seen[numeral]);
        seen[numeral] = true;
        uint8_t opposites = 0;
        for (uint8_t j = 0; j < D20_FACE_COUNT; j++) {
            if (dot(mesh.normals[i], mesh.normals[j]) < -1.0f + TOLERANCE) {
                opposites++;
                TEST_ASSERT_EQUAL_UINT8(OPPOSITE_SUM, numeral + mesh.numerals[j]);
            }
        }
        TEST_ASSERT_EQUAL_UINT8(1, opposites);
    }
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
    RUN_TEST(test_every_edge_has_the_requested_length);
    RUN_TEST(test_the_faces_close_the_body);
    RUN_TEST(test_every_face_winds_outward_and_carries_its_unit_normal);
    RUN_TEST(test_numerals_run_1_to_20_and_opposite_faces_sum_to_21);
    RUN_TEST(test_a_quarter_turn_about_z_takes_x_to_y);
    RUN_TEST(test_a_rotation_keeps_lengths_whatever_its_axis_length);
    RUN_TEST(test_the_identity_moves_nothing);
    return UNITY_END();
}

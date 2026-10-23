#include "d20_mesh.h"

namespace {

constexpr float GOLDEN_RATIO = 1.6180339887f;
// The vertices below, (0, ±1, ±φ) and their cyclic permutations, have edges of length 2.
constexpr float UNIT_EDGE_LENGTH = 2.0f;
constexpr uint8_t HALF_VERTICES = D20_VERTEX_COUNT / 2;
constexpr uint8_t HALF_FACES = D20_FACE_COUNT / 2;
constexpr uint8_t OPPOSITE_NUMERAL_SUM = D20_FACE_COUNT + 1;

// Vertex i + 6 is vertex i through the centre, so only half are listed.
constexpr Vec3 HALF_OF_VERTICES[HALF_VERTICES] = {
    {0, 1, GOLDEN_RATIO}, {0, -1, GOLDEN_RATIO}, {1, GOLDEN_RATIO, 0},
    {-1, GOLDEN_RATIO, 0}, {GOLDEN_RATIO, 0, 1}, {-GOLDEN_RATIO, 0, 1},
};

// Face i + 10 is face i through the centre, so only half are listed: one of each pair of
// opposite faces, wound outward.
constexpr FaceVertices HALF_OF_FACES[HALF_FACES] = {
    {0, 1, 4}, {0, 5, 1}, {0, 2, 3}, {0, 4, 2}, {0, 3, 5},
    {1, 9, 4}, {1, 5, 8}, {1, 8, 9}, {2, 4, 11}, {3, 10, 5},
};

uint8_t oppositeVertex(uint8_t vertex) { return (vertex + HALF_VERTICES) % D20_VERTEX_COUNT; }

} // namespace

D20Mesh makeD20(float edgeLength) {
    D20Mesh mesh{};
    const float scale = edgeLength / UNIT_EDGE_LENGTH;
    for (uint8_t i = 0; i < HALF_VERTICES; i++) {
        mesh.vertices[i] = HALF_OF_VERTICES[i] * scale;
        mesh.vertices[i + HALF_VERTICES] = HALF_OF_VERTICES[i] * -scale;
    }
    for (uint8_t i = 0; i < HALF_FACES; i++) {
        const FaceVertices face = HALF_OF_FACES[i];
        mesh.faces[i] = face;
        // Mirroring through the centre reverses a triangle's winding, so two vertices swap
        // to keep the opposite face wound outward too.
        mesh.faces[i + HALF_FACES] = {oppositeVertex(face.a), oppositeVertex(face.c), oppositeVertex(face.b)};
        mesh.numerals[i] = i + 1;
        mesh.numerals[i + HALF_FACES] = OPPOSITE_NUMERAL_SUM - (i + 1);
    }
    for (uint8_t i = 0; i < D20_FACE_COUNT; i++) {
        const FaceVertices face = mesh.faces[i];
        const Vec3 a = mesh.vertices[face.a];
        mesh.normals[i] = normalized(cross(mesh.vertices[face.b] - a, mesh.vertices[face.c] - a));
    }
    return mesh;
}

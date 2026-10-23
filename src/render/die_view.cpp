#include "die_view.h"

#include <math.h>

namespace {

constexpr Vec3 TOWARD_CAMERA{0, 0, 1};
constexpr int RED_SHIFT = 11;
constexpr int GREEN_SHIFT = 5;
constexpr int DROP_TO_5_BITS = 3;
constexpr int DROP_TO_6_BITS = 2;

int16_t toPixel(float coordinate) { return static_cast<int16_t>(lroundf(coordinate)); }

uint8_t scaleChannel(uint8_t channel, float brightness) {
    return static_cast<uint8_t>(lroundf(channel * brightness));
}

} // namespace

uint16_t shadeRgb565(Rgb base, float brightness) {
    const uint8_t r = scaleChannel(base.r, brightness);
    const uint8_t g = scaleChannel(base.g, brightness);
    const uint8_t b = scaleChannel(base.b, brightness);
    return static_cast<uint16_t>((r >> DROP_TO_5_BITS) << RED_SHIFT | (g >> DROP_TO_6_BITS) << GREEN_SHIFT |
                                 b >> DROP_TO_5_BITS);
}

void viewDie(const D20Mesh& mesh, const Rotation& orientation, Vec3 centrePx, Rgb base, const Lighting& lighting,
             float numeralMinFacing, DieView& view) {
    Vec3 posed[D20_VERTEX_COUNT];
    for (uint8_t i = 0; i < D20_VERTEX_COUNT; i++) posed[i] = rotate(orientation, mesh.vertices[i]);

    view.faceCount = 0;
    for (uint8_t i = 0; i < D20_FACE_COUNT; i++) {
        const Vec3 normal = rotate(orientation, mesh.normals[i]);
        const float facing = dot(normal, TOWARD_CAMERA);
        if (facing <= 0) continue;

        const FaceVertices corners = mesh.faces[i];
        const Vec3 points[TRIANGLE_CORNERS] = {posed[corners.a], posed[corners.b], posed[corners.c]};
        ScreenFace& face = view.faces[view.faceCount++];
        float sumX = 0;
        float sumY = 0;
        for (int corner = 0; corner < TRIANGLE_CORNERS; corner++) {
            // Screen y grows downward, the tray's y upward.
            const float x = centrePx.x + points[corner].x;
            const float y = centrePx.y - points[corner].y;
            face.x[corner] = toPixel(x);
            face.y[corner] = toPixel(y);
            sumX += x;
            sumY += y;
        }
        const float lit = fmaxf(0.0f, dot(normal, lighting.towardLight));
        face.color565 = shadeRgb565(base, lighting.ambient + (1.0f - lighting.ambient) * lit);
        face.showsNumeral = facing >= numeralMinFacing;
        face.numeralX = toPixel(sumX / TRIANGLE_CORNERS);
        face.numeralY = toPixel(sumY / TRIANGLE_CORNERS);
        face.numeral = mesh.numerals[i];
    }
}

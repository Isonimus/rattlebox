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

void viewDie(const DieShape& shape, float pxPerUnit, const Rotation& orientation, Vec3 centrePx, Rgb base,
             const Lighting& lighting, float numeralMinFacing, DieView& view) {
    Vec3 posedPx[DIE_MAX_VERTICES];
    for (uint8_t i = 0; i < shape.vertexCount; i++) posedPx[i] = rotate(orientation, shape.vertices[i]) * pxPerUnit;

    view.faceCount = 0;
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        const DieFace& source = shape.faces[i];
        const Vec3 normal = rotate(orientation, source.normal);
        const float facing = dot(normal, TOWARD_CAMERA);
        if (facing <= 0) continue;

        ScreenFace& face = view.faces[view.faceCount++];
        face.cornerCount = source.cornerCount;
        float sumX = 0;
        float sumY = 0;
        for (uint8_t corner = 0; corner < source.cornerCount; corner++) {
            const Vec3 point = posedPx[source.corners[corner]];
            // Screen y grows downward, the tray's y upward.
            const float x = centrePx.x + point.x;
            const float y = centrePx.y - point.y;
            face.x[corner] = toPixel(x);
            face.y[corner] = toPixel(y);
            sumX += x;
            sumY += y;
        }
        const float lit = fmaxf(0.0f, dot(normal, lighting.towardLight));
        face.color565 = shadeRgb565(base, lighting.ambient + (1.0f - lighting.ambient) * lit);
        face.showsNumeral = facing >= numeralMinFacing;
        face.numeralX = toPixel(sumX / source.cornerCount);
        face.numeralY = toPixel(sumY / source.cornerCount);
        face.numeral = source.numeral;
    }
}

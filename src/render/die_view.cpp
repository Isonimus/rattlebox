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

/** @brief The face turned furthest from the camera: the one an apex-read die is resting on. */
uint8_t restingFaceIndex(const DieShape& shape, const Rotation& orientation) {
    uint8_t resting = 0;
    float least = dot(rotate(orientation, shape.faces[0].normal), TOWARD_CAMERA);
    for (uint8_t i = 1; i < shape.faceCount; i++) {
        const float facing = dot(rotate(orientation, shape.faces[i].normal), TOWARD_CAMERA);
        if (facing < least) {
            least = facing;
            resting = i;
        }
    }
    return resting;
}

bool faceHasCorner(const DieFace& face, uint8_t vertex) {
    for (uint8_t corner = 0; corner < face.cornerCount; corner++) {
        if (face.corners[corner] == vertex) return true;
    }
    return false;
}

/**
 * @brief The vertex @p face does not touch: on a tetrahedron, exactly one.
 *
 * Standing invariant #7 is what makes "exactly one" true, and test_geometry checks it for every
 * catalog entry that declares NumeralReading::Apex, so this returns the first and does not search
 * for a second.
 */
uint8_t vertexOpposite(const DieShape& shape, uint8_t face) {
    for (uint8_t vertex = 0; vertex < shape.vertexCount; vertex++) {
        if (!faceHasCorner(shape.faces[face], vertex)) return vertex;
    }
    return 0;
}

} // namespace

uint16_t shadeRgb565(Rgb base, float brightness) {
    const uint8_t r = scaleChannel(base.r, brightness);
    const uint8_t g = scaleChannel(base.g, brightness);
    const uint8_t b = scaleChannel(base.b, brightness);
    return static_cast<uint16_t>((r >> DROP_TO_5_BITS) << RED_SHIFT | (g >> DROP_TO_6_BITS) << GREEN_SHIFT |
                                 b >> DROP_TO_5_BITS);
}

void viewDie(const DieShape& shape, Numbering numbering, float pxPerUnit, const Rotation& orientation,
             Vec3 centrePx, Rgb base, const Lighting& lighting, float numeralMinFacing, DieView& view) {
    Vec3 posedPx[DIE_MAX_VERTICES];
    for (uint8_t i = 0; i < shape.vertexCount; i++) posedPx[i] = rotate(orientation, shape.vertices[i]) * pxPerUnit;

    // An apex-read die shows one digit, its resting face's, on every visible face that meets the
    // raised corner (ADR-0033). Both are found before the loop because they are facts about the
    // whole die, not about the face being drawn.
    const bool readAtApex = shape.reading == NumeralReading::Apex;
    const uint8_t resting = readAtApex ? restingFaceIndex(shape, orientation) : 0;
    const uint8_t apex = readAtApex ? vertexOpposite(shape, resting) : 0;

    view.faceCount = 0;
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        const DieFace& source = shape.faces[i];
        const Vec3 normal = rotate(orientation, source.normal);
        const float facing = dot(normal, TOWARD_CAMERA);
        if (facing <= FACING_EDGE_ON) continue;

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
        // Both rules put the digit at the face's centre; they differ in which digit, and in
        // whether the face carries one at all (ADR-0033).
        //
        // Drawing an apex-read die's digits nearer the raised corner was tried and measured off
        // the device, and it does not fit: a resting D4 is 45.65 px across, its apex projects to
        // the middle of its own silhouette, and half way from there to each face centre leaves
        // the closest pair of digits 6.21 px apart against a glyph 6 px wide and 8 px tall. The
        // three copies ran together into one unreadable mark. At the face centres they are
        // 12.42 px apart and clear in both axes, which is why this is the centre and not the
        // corner a printed D4 writes on (slice 0032).
        face.numeralX = toPixel(sumX / source.cornerCount);
        face.numeralY = toPixel(sumY / source.cornerCount);
        // Only the faces that will be drawn are formatted. Labelling every face instead cost about
        // 400 us a frame of work on the StickC, 5 to 9 percent across every die type and measured
        // on the device, because snprintf runs about 15 us there and most faces of a die show no
        // numeral at all: a D20 square to the camera draws one of twenty (slice 0035).
        const uint8_t numeral = readAtApex ? shape.faces[resting].numeral : source.numeral;
        face.showsNumeral = readAtApex ? faceHasCorner(source, apex) : facing >= numeralMinFacing;
        // An empty label on a face that shows none, so nothing reads an uninitialised string.
        face.numeralText[0] = '\0';
        if (face.showsNumeral) numeralLabel(numbering, numeral, face.numeralText, sizeof face.numeralText);
    }
}

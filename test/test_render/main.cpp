#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>
#include "../../src/geometry/rotation.h"
#include "../../src/geometry/rotation.cpp"
#include "../../src/geometry/die_numbering.h"
#include "../../src/geometry/die_numbering.cpp"
#include "../../src/geometry/die_shape.h"
#include "../../src/geometry/die_shape.cpp"
#include "../../src/render/die_view.h"
#include "../../src/render/die_view.cpp"
#include "../../src/physics/physics_config.h"
#include "../../src/render/tray_layout.h"
#include "../../src/render/tray_layout.cpp"

// A face's label read back as a number. Every die here but the percentile ones is plain, so the
// label is the numeral the shape carries and these read as they did before labels existed.
static uint8_t drawnNumeral(const ScreenFace& face) { return static_cast<uint8_t>(atoi(face.numeralText)); }

static const float EDGE_PX = 24.0f;
static const Vec3 CENTRE_PX{120, 67, 0};
static const Rgb WHITE{255, 255, 255};
static const Lighting FROM_CAMERA{{0, 0, 1}, 0.25f};
static const float SHOW_ALL_NUMERALS = 0.0f;
// Only a face within about 8° of the camera reads as facing it.
static const float ONLY_THE_TOP_FACE = 0.99f;
static const uint8_t TOP_FACE = 7;
static const uint8_t OPPOSITE_SUM = 21;
// The tests build the die in pixels, so it draws at a scale of one.
static const float PX_PER_UNIT = 1.0f;
// The die numerals are the built-in 6 x 8 font at size 1 (src/render/band_drawing.cpp), so a
// face with less area than one glyph cannot show a digit whatever is computed for it. Doubled,
// because twiceAreaPx is.
static const long TWICE_A_GLYPH_PX = 2 * 6 * 8;

/** @brief The orientation that turns face @p face of @p mesh straight toward the camera. */
static Rotation faceTowardCamera(const DieShape& shape, uint8_t face) {
    const Vec3 normal = shape.faces[face].normal;
    return rotationAbout(cross(normal, {0, 0, 1}), acosf(normal.z));
}

static Vec3 normalOfNumeral(const DieShape& shape, uint8_t numeral) {
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        if (shape.faces[i].numeral == numeral) return shape.faces[i].normal;
    }
    TEST_FAIL_MESSAGE("no face carries that numeral");
    return {};
}

static Vec3 centroidOfNumeral(const DieShape& shape, uint8_t numeral) {
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        if (shape.faces[i].numeral == numeral) return faceCentre(shape, i);
    }
    TEST_FAIL_MESSAGE("no face carries that numeral");
    return {};
}

/** @brief The orientation that turns face @p face of @p shape straight away from the camera. */
static Rotation faceFromCamera(const DieShape& shape, uint8_t face) {
    const Vec3 normal = shape.faces[face].normal;
    return rotationAbout(cross(normal, {0, 0, -1}), acosf(-normal.z));
}

/** @brief Whether (@p x, @p y) lies inside the screen-space polygon of @p face. */
static bool insideFace(const ScreenFace& face, int16_t x, int16_t y) {
    // A convex polygon wound consistently keeps every inside point on the same side of each edge.
    bool anyLeft = false;
    bool anyRight = false;
    for (uint8_t corner = 0; corner < face.cornerCount; corner++) {
        const int16_t fromX = face.x[corner], fromY = face.y[corner];
        const int16_t toX = face.x[(corner + 1) % face.cornerCount], toY = face.y[(corner + 1) % face.cornerCount];
        const long side = static_cast<long>(toX - fromX) * (y - fromY) - static_cast<long>(toY - fromY) * (x - fromX);
        if (side > 0) anyRight = true;
        if (side < 0) anyLeft = true;
    }
    return !(anyLeft && anyRight);
}

/** @brief Twice the area of @p face as drawn, by the shoelace formula; sign-free. */
static long twiceAreaPx(const ScreenFace& face) {
    long twice = 0;
    for (uint8_t corner = 0; corner < face.cornerCount; corner++) {
        const uint8_t next = (corner + 1) % face.cornerCount;
        twice += static_cast<long>(face.x[corner]) * face.y[next] - static_cast<long>(face.x[next]) * face.y[corner];
    }
    return twice < 0 ? -twice : twice;
}

static uint8_t faceCountTowardCamera(const DieShape& shape, const Rotation& orientation) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        if (rotate(orientation, shape.faces[i].normal).z > FACING_EDGE_ON) count++;
    }
    return count;
}

void test_at_rest_eight_faces_point_up_four_edge_on_and_eight_down() {
    // An icosahedron's face normals point at a dodecahedron's vertices. In the mesh's rest pose
    // those are (±1, ±1, ±1), (0, ±1/φ, ±φ), (±1/φ, ±φ, 0) and (±φ, 0, ±1/φ), up to scale: 4 + 2 + 2
    // point toward the camera, and the 4 with z = 0 are edge-on and not drawn. Counted by hand,
    // rather than by the same test the renderer makes, so drawing edge-on faces cannot pass;
    // drawing the far side instead is the opposite-face test's job, since it also counts 8.
    const DieShape mesh = makeD20(EDGE_PX);
    DieView view{};
    viewDie(mesh, Numbering::Plain, PX_PER_UNIT, IDENTITY_ROTATION, CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
    TEST_ASSERT_EQUAL_UINT8(8, view.faceCount);
}

void test_only_faces_toward_the_camera_are_drawn() {
    const DieShape mesh = makeD20(EDGE_PX);
    const Rotation orientations[] = {IDENTITY_ROTATION, rotationAbout({1, 2, 3}, 0.7f), faceTowardCamera(mesh, TOP_FACE)};
    for (const Rotation& orientation : orientations) {
        DieView view{};
        viewDie(mesh, Numbering::Plain, PX_PER_UNIT, orientation, CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
        TEST_ASSERT_EQUAL_UINT8(faceCountTowardCamera(mesh, orientation), view.faceCount);
    }
}

void test_a_face_rounding_tips_toward_the_camera_is_edge_on_and_not_drawn() {
    // A D20 at rest has four faces exactly edge-on. Where the compiler fuses multiply-adds, their
    // computed normals come out a hair either side of perpendicular (z = 4.8e-9, measured on x86-64
    // with FMA), and a facing test at exactly zero then draws two of them: CI's runner drew 10 faces
    // where the rest pose has 8.
    const float ROUNDED_EDGE_ON_Z = 4.8e-9f;
    DieShape sliver = makeD20(EDGE_PX);
    sliver.faceCount = 1;
    sliver.faces[0].normal = {1.0f, 0.0f, ROUNDED_EDGE_ON_Z};
    DieView view{};
    viewDie(sliver, Numbering::Plain, PX_PER_UNIT, IDENTITY_ROTATION, CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
    TEST_ASSERT_EQUAL_UINT8(0, view.faceCount);
}

void test_a_face_turned_to_the_camera_is_drawn_and_its_opposite_is_not() {
    const DieShape mesh = makeD20(EDGE_PX);
    DieView view{};
    viewDie(mesh, Numbering::Plain, PX_PER_UNIT, faceTowardCamera(mesh, TOP_FACE), CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
    bool topDrawn = false;
    for (uint8_t i = 0; i < view.faceCount; i++) {
        TEST_ASSERT_NOT_EQUAL(OPPOSITE_SUM - mesh.faces[TOP_FACE].numeral, drawnNumeral(view.faces[i]));
        if (drawnNumeral(view.faces[i]) == mesh.faces[TOP_FACE].numeral) topDrawn = true;
    }
    TEST_ASSERT_TRUE(topDrawn);
}

void test_only_faces_within_the_numeral_angle_show_a_numeral_at_their_centre() {
    const DieShape mesh = makeD20(EDGE_PX);
    DieView view{};
    viewDie(mesh, Numbering::Plain, PX_PER_UNIT, faceTowardCamera(mesh, TOP_FACE), CENTRE_PX, WHITE, FROM_CAMERA, ONLY_THE_TOP_FACE, view);
    uint8_t shown = 0;
    for (uint8_t i = 0; i < view.faceCount; i++) {
        const ScreenFace& face = view.faces[i];
        if (!face.showsNumeral) continue;
        shown++;
        TEST_ASSERT_EQUAL_UINT8(mesh.faces[TOP_FACE].numeral, drawnNumeral(face));
        // A face square to the camera is centred over the die's centre.
        TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(CENTRE_PX.x), face.numeralX);
        TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(CENTRE_PX.y), face.numeralY);
    }
    TEST_ASSERT_EQUAL_UINT8(1, shown);
}

void test_each_numeral_sits_over_its_own_face_with_screen_y_down() {
    // The die's outline is symmetric, so a mirrored picture would put the same corners on
    // the same pixels; the numerals are not, so each is checked against its own face's
    // centre, with the tray's up drawn toward the screen's top.
    const DieShape mesh = makeD20(EDGE_PX);
    const Rotation orientation = rotationAbout({1, 2, 3}, 0.7f);
    DieView view{};
    viewDie(mesh, Numbering::Plain, PX_PER_UNIT, orientation, CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
    TEST_ASSERT_TRUE(view.faceCount > 1);
    for (uint8_t i = 0; i < view.faceCount; i++) {
        const ScreenFace& face = view.faces[i];
        const Vec3 centre = rotate(orientation, centroidOfNumeral(mesh, drawnNumeral(face)));
        TEST_ASSERT_EQUAL_INT16(lroundf(CENTRE_PX.x + centre.x), face.numeralX);
        TEST_ASSERT_EQUAL_INT16(lroundf(CENTRE_PX.y - centre.y), face.numeralY);
    }
}

void test_a_resting_d4_shows_its_hidden_resting_numeral_on_every_visible_face() {
    // ADR-0033: the three faces a player can see all carry the same digit, the one on the face
    // underneath, as a printed D4 does. The resting face itself faces away and is not drawn.
    const DieShape d4 = makeD4(EDGE_PX);
    for (uint8_t resting = 0; resting < d4.faceCount; resting++) {
        DieView view;
        viewDie(d4, Numbering::Plain, PX_PER_UNIT, faceFromCamera(d4, resting), CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
        TEST_ASSERT_EQUAL_UINT8(3, view.faceCount);
        for (uint8_t i = 0; i < view.faceCount; i++) {
            TEST_ASSERT_TRUE_MESSAGE(view.faces[i].showsNumeral, "a visible face of a resting D4 shows no numeral");
            TEST_ASSERT_EQUAL_UINT8(d4.faces[resting].numeral, drawnNumeral(view.faces[i]));
            TEST_ASSERT_TRUE_MESSAGE(insideFace(view.faces[i], view.faces[i].numeralX, view.faces[i].numeralY),
                                     "an apex numeral falls outside its own face");
        }
    }
}

void test_a_resting_d4_draws_its_numerals_however_steep_the_numeral_angle_is_set() {
    // The sloping faces of a resting D4 face the camera at a third, under the shipped 0.5, so a
    // threshold rule would draw the die blank. The apex rule does not use one (ADR-0033).
    const DieShape d4 = makeD4(EDGE_PX);
    DieView view;
    viewDie(d4, Numbering::Plain, PX_PER_UNIT, faceFromCamera(d4, 0), CENTRE_PX, WHITE, FROM_CAMERA, ONLY_THE_TOP_FACE, view);
    TEST_ASSERT_EQUAL_UINT8(3, view.faceCount);
    for (uint8_t i = 0; i < view.faceCount; i++) TEST_ASSERT_TRUE(view.faces[i].showsNumeral);
    // And the same pose under the old rule: every visible face is too steep to carry a numeral.
    for (uint8_t i = 0; i < view.faceCount; i++) {
        const Vec3 normal = rotate(faceFromCamera(d4, 0), d4.faces[i].normal);
        if (normal.z > FACING_EDGE_ON) TEST_ASSERT_TRUE(normal.z < 0.5f);
    }
}

void test_a_d4_never_draws_a_face_without_the_apex_whatever_its_orientation() {
    // The only face the apex does not touch is the resting face, and the resting face is by
    // definition the one turned furthest from the camera, so it is never among the faces drawn
    // (ADR-0033). That makes "every visible face carries the result" true at every orientation,
    // not only at rest, which is what keeps a tumbling die from flashing a digit it will not
    // settle on. Swept rather than spot-checked, because the claim is about all of them.
    const DieShape d4 = makeD4(EDGE_PX);
    const uint8_t SWEEP = 12;
    const float TURN_RAD = 0.37f;
    // At least one face big enough for a digit at every step of the sweep, so an exemption that
    // grew to cover everything would fail here rather than pass silently. The sweep as written
    // clears this with 20 faces; the floor is the claim, not the count.
    const int FACES_WORTH_CHECKING = SWEEP;
    int checkedFaces = 0;
    Rotation orientation = IDENTITY_ROTATION;
    for (uint8_t step = 0; step < SWEEP; step++) {
        orientation = rotationAbout({1, 2, 3}, TURN_RAD * step);
        DieView view;
        viewDie(d4, Numbering::Plain, PX_PER_UNIT, orientation, CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
        TEST_ASSERT_TRUE(view.faceCount >= 1);
        // The face turned furthest from the camera is the one the die would read here.
        uint8_t resting = 0;
        for (uint8_t i = 1; i < d4.faceCount; i++) {
            if (rotate(orientation, d4.faces[i].normal).z < rotate(orientation, d4.faces[resting].normal).z) resting = i;
        }
        for (uint8_t i = 0; i < view.faceCount; i++) {
            const ScreenFace& face = view.faces[i];
            TEST_ASSERT_TRUE_MESSAGE(face.showsNumeral, "a visible face of a D4 shows no numeral");
            TEST_ASSERT_EQUAL_UINT8(d4.faces[resting].numeral, drawnNumeral(face));
            // The mean of a convex polygon's corners is always inside it, so the only way the
            // drawn point can fall outside is a face projected thinner than the pixel grid it is
            // rounded onto: a tumbling D4 turned edge-on measured 1.5 px of area and put the
            // point 3 px off. A face that small cannot show a 6 x 8 digit however the position
            // is computed, so the claim is made where it means something, and checkedFaces below
            // keeps that from quietly becoming no faces at all.
            if (twiceAreaPx(face) < TWICE_A_GLYPH_PX) continue;
            checkedFaces++;
            TEST_ASSERT_TRUE_MESSAGE(insideFace(face, face.numeralX, face.numeralY),
                                     "an apex numeral falls outside a face wide enough to hold it");
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL_INT(FACES_WORTH_CHECKING, checkedFaces);
}

void test_a_top_face_die_still_hides_numerals_on_faces_too_steep_to_read() {
    // The D4 ignores numeralMinFacing; the other five must not start ignoring it too.
    const DieShape d6 = makeD6(EDGE_PX);
    const float HALF_RIGHT_ANGLE_RAD = 0.7853982f;
    DieView view;
    viewDie(d6, Numbering::Plain, PX_PER_UNIT, rotationAbout({1, 0, 0}, HALF_RIGHT_ANGLE_RAD), CENTRE_PX, WHITE, FROM_CAMERA,
            ONLY_THE_TOP_FACE, view);
    // Two faces at 45° to the camera: drawn, but neither square enough to carry a numeral.
    TEST_ASSERT_EQUAL_UINT8(2, view.faceCount);
    for (uint8_t i = 0; i < view.faceCount; i++) TEST_ASSERT_FALSE(view.faces[i].showsNumeral);
}

void test_a_face_square_to_the_light_is_the_full_colour() {
    const DieShape mesh = makeD20(EDGE_PX);
    DieView view{};
    viewDie(mesh, Numbering::Plain, PX_PER_UNIT, faceTowardCamera(mesh, TOP_FACE), CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
    for (uint8_t i = 0; i < view.faceCount; i++) {
        if (drawnNumeral(view.faces[i]) == mesh.faces[TOP_FACE].numeral) TEST_ASSERT_EQUAL_HEX16(0xFFFF, view.faces[i].color565);
    }
}

void test_a_face_turned_from_the_light_keeps_the_ambient_floor() {
    // Lit from below, no front face catches the light, so each is the ambient shade, not black.
    const DieShape mesh = makeD20(EDGE_PX);
    const Lighting fromBelow{{0, 0, -1}, 0.25f};
    DieView view{};
    viewDie(mesh, Numbering::Plain, PX_PER_UNIT, IDENTITY_ROTATION, CENTRE_PX, WHITE, fromBelow, SHOW_ALL_NUMERALS, view);
    TEST_ASSERT_TRUE(view.faceCount > 0);
    for (uint8_t i = 0; i < view.faceCount; i++) TEST_ASSERT_EQUAL_HEX16(shadeRgb565(WHITE, 0.25f), view.faces[i].color565);
}

void test_a_face_more_square_to_the_light_is_never_darker() {
    // Lit from the camera, how square a face is to the light is its normal's z.
    const DieShape mesh = makeD20(EDGE_PX);
    const Rotation orientation = rotationAbout({1, 2, 3}, 0.7f);
    const uint16_t BLUE_MASK = 0x1F;
    DieView view{};
    viewDie(mesh, Numbering::Plain, PX_PER_UNIT, orientation, CENTRE_PX, {0, 0, 255}, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
    TEST_ASSERT_TRUE(view.faceCount > 1);
    for (uint8_t i = 0; i < view.faceCount; i++) {
        for (uint8_t j = 0; j < view.faceCount; j++) {
            const float squarenessI = rotate(orientation, normalOfNumeral(mesh, drawnNumeral(view.faces[i]))).z;
            const float squarenessJ = rotate(orientation, normalOfNumeral(mesh, drawnNumeral(view.faces[j]))).z;
            if (squarenessI > squarenessJ) {
                TEST_ASSERT_TRUE((view.faces[i].color565 & BLUE_MASK) >= (view.faces[j].color565 & BLUE_MASK));
            }
        }
    }
}

void test_a_d6_square_to_the_camera_shows_one_square_face_with_its_corners_and_numeral() {
    // At rest the D6's +z face, numeral 1, looks at the camera and its four sides are edge-on.
    const DieShape d6 = makeD6(EDGE_PX);
    const int16_t half = static_cast<int16_t>(EDGE_PX / 2);
    DieView view{};
    viewDie(d6, Numbering::Plain, PX_PER_UNIT, IDENTITY_ROTATION, CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
    TEST_ASSERT_EQUAL_UINT8(1, view.faceCount);
    const ScreenFace& face = view.faces[0];
    TEST_ASSERT_EQUAL_UINT8(1, drawnNumeral(face));
    TEST_ASSERT_EQUAL_UINT8(4, face.cornerCount);
    for (uint8_t corner = 0; corner < face.cornerCount; corner++) {
        TEST_ASSERT_EQUAL_INT16(half, abs(face.x[corner] - static_cast<int16_t>(CENTRE_PX.x)));
        TEST_ASSERT_EQUAL_INT16(half, abs(face.y[corner] - static_cast<int16_t>(CENTRE_PX.y)));
    }
    TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(CENTRE_PX.x), face.numeralX);
    TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(CENTRE_PX.y), face.numeralY);
}

void test_a_die_in_millimetres_draws_like_the_same_die_in_pixels() {
    const float pxPerMm = 2.5f;
    const Rotation orientation = rotationAbout({1, 2, 3}, 0.7f);
    DieView inPixels{};
    DieView inMillimetres{};
    viewDie(makeD20(EDGE_PX), Numbering::Plain, PX_PER_UNIT, orientation, CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, inPixels);
    viewDie(makeD20(EDGE_PX / pxPerMm), Numbering::Plain, pxPerMm, orientation, CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS,
            inMillimetres);
    TEST_ASSERT_EQUAL_UINT8(inPixels.faceCount, inMillimetres.faceCount);
    for (uint8_t i = 0; i < inPixels.faceCount; i++) {
        TEST_ASSERT_EQUAL_INT16_ARRAY(inPixels.faces[i].x, inMillimetres.faces[i].x, inPixels.faces[i].cornerCount);
        TEST_ASSERT_EQUAL_INT16_ARRAY(inPixels.faces[i].y, inMillimetres.faces[i].y, inPixels.faces[i].cornerCount);
    }
}

void test_rgb565_packs_the_top_bits_of_each_channel() {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, shadeRgb565({255, 255, 255}, 1.0f));
    TEST_ASSERT_EQUAL_HEX16(0xF800, shadeRgb565({255, 0, 0}, 1.0f));
    TEST_ASSERT_EQUAL_HEX16(0x07E0, shadeRgb565({0, 255, 0}, 1.0f));
    TEST_ASSERT_EQUAL_HEX16(0x001F, shadeRgb565({0, 0, 255}, 1.0f));
    TEST_ASSERT_EQUAL_HEX16(0x0000, shadeRgb565({255, 255, 255}, 0.0f));
    // Half of 255 rounds to 128, whose top five bits are 16.
    TEST_ASSERT_EQUAL_HEX16(0x8000, shadeRgb565({255, 0, 0}, 0.5f));
}

// The layout ADR-0016 decided, in the screen's own pixels: a 20 px strip over a tray as wide as the
// 240 x 135 px screen, at ADR-0007's 24 px D20 edge.
static const float STRIP_BOTTOM_PX = 20.0f;
static const float SCREEN_BOTTOM_PX = 135.0f;
static const float SCREEN_RIGHT_PX = 240.0f;
static const float D20_EDGE_PX = 24.0f;
static const float LAYOUT_TOLERANCE_PX = 0.01f;

void test_the_tray_fills_the_screen_under_the_strip_at_the_d20s_24_px_edge() {
    const Vec3 nearLeft = trayToScreenPx({0, 0, 0});
    const Vec3 farRight = trayToScreenPx({TRAY_WIDTH_MM, TRAY_DEPTH_MM, 0});
    TEST_ASSERT_FLOAT_WITHIN(LAYOUT_TOLERANCE_PX, 0, nearLeft.x);
    TEST_ASSERT_FLOAT_WITHIN(LAYOUT_TOLERANCE_PX, SCREEN_BOTTOM_PX, nearLeft.y);
    TEST_ASSERT_FLOAT_WITHIN(LAYOUT_TOLERANCE_PX, SCREEN_RIGHT_PX, farRight.x);
    // The far wall is the strip's bottom edge, so no die against it is drawn under the band.
    TEST_ASSERT_FLOAT_WITHIN(LAYOUT_TOLERANCE_PX, STRIP_BOTTOM_PX, farRight.y);
    const Vec3 oneEdgeAlong = trayToScreenPx({D20_EDGE_MM, 0, D20_EDGE_MM});
    TEST_ASSERT_FLOAT_WITHIN(LAYOUT_TOLERANCE_PX, D20_EDGE_PX, oneEdgeAlong.x);
    TEST_ASSERT_FLOAT_WITHIN(LAYOUT_TOLERANCE_PX, D20_EDGE_PX, oneEdgeAlong.z);
}

static uint8_t faceIndexOfNumeral(const DieShape& shape, uint8_t numeral) {
    for (uint8_t i = 0; i < shape.faceCount; i++) {
        if (shape.faces[i].numeral == numeral) return i;
    }
    TEST_FAIL_MESSAGE("no face carries that numeral");
    return 0;
}

/** @brief The label the one face turned square to the camera is drawn with, under @p numbering. */
static void labelOfTopFace(const DieShape& shape, Numbering numbering, uint8_t numeral, char* out, size_t size) {
    DieView view{};
    viewDie(shape, numbering, PX_PER_UNIT, faceTowardCamera(shape, faceIndexOfNumeral(shape, numeral)), CENTRE_PX,
            WHITE, FROM_CAMERA, ONLY_THE_TOP_FACE, view);
    uint8_t drawn = 0;
    for (uint8_t i = 0; i < view.faceCount; i++) {
        if (!view.faces[i].showsNumeral) continue;
        snprintf(out, size, "%s", view.faces[i].numeralText);
        drawn++;
    }
    TEST_ASSERT_EQUAL_UINT8(1, drawn);
}

// Slice 0035. A percentile body's faces show what its numbering says, never the 1 to 10 the shape
// carries: the shape is a D10 and only the labels tell a tens body from a units body (ADR-0034).
void test_a_percentile_tens_body_draws_its_faces_as_00_to_90() {
    const DieShape d10 = makeD10(EDGE_PX);
    char label[NUMERAL_LABEL_MAX_CHARS + 1];
    labelOfTopFace(d10, Numbering::PercentileTens, 1, label, sizeof label);
    TEST_ASSERT_EQUAL_STRING("00", label);
    labelOfTopFace(d10, Numbering::PercentileTens, 8, label, sizeof label);
    TEST_ASSERT_EQUAL_STRING("70", label);
    labelOfTopFace(d10, Numbering::PercentileTens, 10, label, sizeof label);
    TEST_ASSERT_EQUAL_STRING("90", label);
}

void test_a_percentile_units_body_draws_its_faces_as_0_to_9() {
    const DieShape d10 = makeD10(EDGE_PX);
    char label[NUMERAL_LABEL_MAX_CHARS + 1];
    labelOfTopFace(d10, Numbering::PercentileUnits, 1, label, sizeof label);
    TEST_ASSERT_EQUAL_STRING("0", label);
    labelOfTopFace(d10, Numbering::PercentileUnits, 4, label, sizeof label);
    TEST_ASSERT_EQUAL_STRING("3", label);
    labelOfTopFace(d10, Numbering::PercentileUnits, 10, label, sizeof label);
    TEST_ASSERT_EQUAL_STRING("9", label);
}

// Whatever a percentile body's orientation, every face it draws reads as its own numbering and
// never as the shape's numeral. A tens body is two characters at every face, a units body one, so
// a numbering that leaked through would show up as a label of the wrong width.
void test_a_percentile_bodys_labels_never_read_as_the_shapes_numerals() {
    const DieShape d10 = makeD10(EDGE_PX);
    const uint8_t SWEEP = 12;
    const float TURN_RAD = 0.37f;
    unsigned checkedFaces = 0;
    for (uint8_t step = 0; step < SWEEP; step++) {
        const Rotation orientation = rotationAbout({1, 2, 3}, TURN_RAD * step);
        DieView tens{};
        DieView units{};
        viewDie(d10, Numbering::PercentileTens, PX_PER_UNIT, orientation, CENTRE_PX, WHITE, FROM_CAMERA,
                SHOW_ALL_NUMERALS, tens);
        viewDie(d10, Numbering::PercentileUnits, PX_PER_UNIT, orientation, CENTRE_PX, WHITE, FROM_CAMERA,
                SHOW_ALL_NUMERALS, units);
        TEST_ASSERT_EQUAL_UINT8(tens.faceCount, units.faceCount);
        for (uint8_t i = 0; i < tens.faceCount; i++) {
            if (!tens.faces[i].showsNumeral) continue;
            const long tensValue = atol(tens.faces[i].numeralText);
            TEST_ASSERT_EQUAL_UINT(2, strlen(tens.faces[i].numeralText));
            TEST_ASSERT_EQUAL_INT(0, tensValue % 10);
            TEST_ASSERT_LESS_OR_EQUAL_INT(90, tensValue);
            const long unitsValue = atol(units.faces[i].numeralText);
            TEST_ASSERT_EQUAL_UINT(1, strlen(units.faces[i].numeralText));
            TEST_ASSERT_LESS_OR_EQUAL_INT(9, unitsValue);
            // The two bodies are the same solid in the same pose, so a face's two labels are the
            // two halves of one numeral: the tens of (n - 1) * 10 and the units of n - 1.
            TEST_ASSERT_EQUAL_INT(tensValue / 10, unitsValue);
            checkedFaces++;
        }
    }
    // The sweep must actually have drawn numerals at every step, or the loop above asserted
    // nothing and would pass just as well with the labels removed. The floor is the claim, not
    // the count the sweep happens to reach.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(SWEEP, checkedFaces);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_percentile_tens_body_draws_its_faces_as_00_to_90);
    RUN_TEST(test_a_percentile_units_body_draws_its_faces_as_0_to_9);
    RUN_TEST(test_a_percentile_bodys_labels_never_read_as_the_shapes_numerals);
    RUN_TEST(test_at_rest_eight_faces_point_up_four_edge_on_and_eight_down);
    RUN_TEST(test_only_faces_toward_the_camera_are_drawn);
    RUN_TEST(test_a_face_rounding_tips_toward_the_camera_is_edge_on_and_not_drawn);
    RUN_TEST(test_a_face_turned_to_the_camera_is_drawn_and_its_opposite_is_not);
    RUN_TEST(test_only_faces_within_the_numeral_angle_show_a_numeral_at_their_centre);
    RUN_TEST(test_each_numeral_sits_over_its_own_face_with_screen_y_down);
    RUN_TEST(test_a_resting_d4_shows_its_hidden_resting_numeral_on_every_visible_face);
    RUN_TEST(test_a_resting_d4_draws_its_numerals_however_steep_the_numeral_angle_is_set);
    RUN_TEST(test_a_d4_never_draws_a_face_without_the_apex_whatever_its_orientation);
    RUN_TEST(test_a_top_face_die_still_hides_numerals_on_faces_too_steep_to_read);
    RUN_TEST(test_a_face_square_to_the_light_is_the_full_colour);
    RUN_TEST(test_a_face_turned_from_the_light_keeps_the_ambient_floor);
    RUN_TEST(test_a_face_more_square_to_the_light_is_never_darker);
    RUN_TEST(test_a_d6_square_to_the_camera_shows_one_square_face_with_its_corners_and_numeral);
    RUN_TEST(test_a_die_in_millimetres_draws_like_the_same_die_in_pixels);
    RUN_TEST(test_rgb565_packs_the_top_bits_of_each_channel);
    RUN_TEST(test_the_tray_fills_the_screen_under_the_strip_at_the_d20s_24_px_edge);
    return UNITY_END();
}

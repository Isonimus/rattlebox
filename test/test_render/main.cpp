#include <stdlib.h>
#include <unity.h>
#include "../../src/geometry/rotation.h"
#include "../../src/geometry/rotation.cpp"
#include "../../src/geometry/die_shape.h"
#include "../../src/geometry/die_shape.cpp"
#include "../../src/render/die_view.h"
#include "../../src/render/die_view.cpp"
#include "../../src/physics/physics_config.h"
#include "../../src/render/tray_layout.h"
#include "../../src/render/tray_layout.cpp"

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
    viewDie(mesh, PX_PER_UNIT, IDENTITY_ROTATION, CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
    TEST_ASSERT_EQUAL_UINT8(8, view.faceCount);
}

void test_only_faces_toward_the_camera_are_drawn() {
    const DieShape mesh = makeD20(EDGE_PX);
    const Rotation orientations[] = {IDENTITY_ROTATION, rotationAbout({1, 2, 3}, 0.7f), faceTowardCamera(mesh, TOP_FACE)};
    for (const Rotation& orientation : orientations) {
        DieView view{};
        viewDie(mesh, PX_PER_UNIT, orientation, CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
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
    viewDie(sliver, PX_PER_UNIT, IDENTITY_ROTATION, CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
    TEST_ASSERT_EQUAL_UINT8(0, view.faceCount);
}

void test_a_face_turned_to_the_camera_is_drawn_and_its_opposite_is_not() {
    const DieShape mesh = makeD20(EDGE_PX);
    DieView view{};
    viewDie(mesh, PX_PER_UNIT, faceTowardCamera(mesh, TOP_FACE), CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
    bool topDrawn = false;
    for (uint8_t i = 0; i < view.faceCount; i++) {
        TEST_ASSERT_NOT_EQUAL(OPPOSITE_SUM - mesh.faces[TOP_FACE].numeral, view.faces[i].numeral);
        if (view.faces[i].numeral == mesh.faces[TOP_FACE].numeral) topDrawn = true;
    }
    TEST_ASSERT_TRUE(topDrawn);
}

void test_only_faces_within_the_numeral_angle_show_a_numeral_at_their_centre() {
    const DieShape mesh = makeD20(EDGE_PX);
    DieView view{};
    viewDie(mesh, PX_PER_UNIT, faceTowardCamera(mesh, TOP_FACE), CENTRE_PX, WHITE, FROM_CAMERA, ONLY_THE_TOP_FACE, view);
    uint8_t shown = 0;
    for (uint8_t i = 0; i < view.faceCount; i++) {
        const ScreenFace& face = view.faces[i];
        if (!face.showsNumeral) continue;
        shown++;
        TEST_ASSERT_EQUAL_UINT8(mesh.faces[TOP_FACE].numeral, face.numeral);
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
    viewDie(mesh, PX_PER_UNIT, orientation, CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
    TEST_ASSERT_TRUE(view.faceCount > 1);
    for (uint8_t i = 0; i < view.faceCount; i++) {
        const ScreenFace& face = view.faces[i];
        const Vec3 centre = rotate(orientation, centroidOfNumeral(mesh, face.numeral));
        TEST_ASSERT_EQUAL_INT16(lroundf(CENTRE_PX.x + centre.x), face.numeralX);
        TEST_ASSERT_EQUAL_INT16(lroundf(CENTRE_PX.y - centre.y), face.numeralY);
    }
}

void test_a_face_square_to_the_light_is_the_full_colour() {
    const DieShape mesh = makeD20(EDGE_PX);
    DieView view{};
    viewDie(mesh, PX_PER_UNIT, faceTowardCamera(mesh, TOP_FACE), CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
    for (uint8_t i = 0; i < view.faceCount; i++) {
        if (view.faces[i].numeral == mesh.faces[TOP_FACE].numeral) TEST_ASSERT_EQUAL_HEX16(0xFFFF, view.faces[i].color565);
    }
}

void test_a_face_turned_from_the_light_keeps_the_ambient_floor() {
    // Lit from below, no front face catches the light, so each is the ambient shade, not black.
    const DieShape mesh = makeD20(EDGE_PX);
    const Lighting fromBelow{{0, 0, -1}, 0.25f};
    DieView view{};
    viewDie(mesh, PX_PER_UNIT, IDENTITY_ROTATION, CENTRE_PX, WHITE, fromBelow, SHOW_ALL_NUMERALS, view);
    TEST_ASSERT_TRUE(view.faceCount > 0);
    for (uint8_t i = 0; i < view.faceCount; i++) TEST_ASSERT_EQUAL_HEX16(shadeRgb565(WHITE, 0.25f), view.faces[i].color565);
}

void test_a_face_more_square_to_the_light_is_never_darker() {
    // Lit from the camera, how square a face is to the light is its normal's z.
    const DieShape mesh = makeD20(EDGE_PX);
    const Rotation orientation = rotationAbout({1, 2, 3}, 0.7f);
    const uint16_t BLUE_MASK = 0x1F;
    DieView view{};
    viewDie(mesh, PX_PER_UNIT, orientation, CENTRE_PX, {0, 0, 255}, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
    TEST_ASSERT_TRUE(view.faceCount > 1);
    for (uint8_t i = 0; i < view.faceCount; i++) {
        for (uint8_t j = 0; j < view.faceCount; j++) {
            const float squarenessI = rotate(orientation, normalOfNumeral(mesh, view.faces[i].numeral)).z;
            const float squarenessJ = rotate(orientation, normalOfNumeral(mesh, view.faces[j].numeral)).z;
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
    viewDie(d6, PX_PER_UNIT, IDENTITY_ROTATION, CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, view);
    TEST_ASSERT_EQUAL_UINT8(1, view.faceCount);
    const ScreenFace& face = view.faces[0];
    TEST_ASSERT_EQUAL_UINT8(1, face.numeral);
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
    viewDie(makeD20(EDGE_PX), PX_PER_UNIT, orientation, CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS, inPixels);
    viewDie(makeD20(EDGE_PX / pxPerMm), pxPerMm, orientation, CENTRE_PX, WHITE, FROM_CAMERA, SHOW_ALL_NUMERALS,
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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_at_rest_eight_faces_point_up_four_edge_on_and_eight_down);
    RUN_TEST(test_only_faces_toward_the_camera_are_drawn);
    RUN_TEST(test_a_face_rounding_tips_toward_the_camera_is_edge_on_and_not_drawn);
    RUN_TEST(test_a_face_turned_to_the_camera_is_drawn_and_its_opposite_is_not);
    RUN_TEST(test_only_faces_within_the_numeral_angle_show_a_numeral_at_their_centre);
    RUN_TEST(test_each_numeral_sits_over_its_own_face_with_screen_y_down);
    RUN_TEST(test_a_face_square_to_the_light_is_the_full_colour);
    RUN_TEST(test_a_face_turned_from_the_light_keeps_the_ambient_floor);
    RUN_TEST(test_a_face_more_square_to_the_light_is_never_darker);
    RUN_TEST(test_a_d6_square_to_the_camera_shows_one_square_face_with_its_corners_and_numeral);
    RUN_TEST(test_a_die_in_millimetres_draws_like_the_same_die_in_pixels);
    RUN_TEST(test_rgb565_packs_the_top_bits_of_each_channel);
    RUN_TEST(test_the_tray_fills_the_screen_under_the_strip_at_the_d20s_24_px_edge);
    return UNITY_END();
}

#include "die_drawing.h"

void drawDieFaces(LGFX_Sprite& canvas, const DieView& view) {
    for (uint8_t i = 0; i < view.faceCount; i++) {
        const ScreenFace& face = view.faces[i];
        // A convex polygon is a fan of triangles from its first corner.
        for (uint8_t corner = 1; corner + 1 < face.cornerCount; corner++) {
            canvas.fillTriangle(face.x[0], face.y[0], face.x[corner], face.y[corner], face.x[corner + 1],
                                face.y[corner + 1], face.color565);
        }
    }
}

void drawDieNumerals(LGFX_Sprite& canvas, const DieView& view, uint16_t numeralColor565) {
    canvas.setTextDatum(textdatum_t::middle_center);
    // No background colour: the text is drawn over the face, not in a box.
    canvas.setTextColor(numeralColor565);
    for (uint8_t i = 0; i < view.faceCount; i++) {
        const ScreenFace& face = view.faces[i];
        // drawString, not drawNumber: a tens body's "00" is two characters and not the number
        // zero (ADR-0034). The datum above centres either length, as the D10's "10" already was.
        if (face.showsNumeral) canvas.drawString(face.numeralText, face.numeralX, face.numeralY);
    }
}

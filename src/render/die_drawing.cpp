#include "die_drawing.h"

void drawDieFaces(LGFX_Sprite& canvas, const DieView& view) {
    for (uint8_t i = 0; i < view.faceCount; i++) {
        const ScreenFace& face = view.faces[i];
        canvas.fillTriangle(face.x[0], face.y[0], face.x[1], face.y[1], face.x[2], face.y[2], face.color565);
    }
}

void drawDieNumerals(LGFX_Sprite& canvas, const DieView& view, uint16_t numeralColor565) {
    canvas.setTextDatum(textdatum_t::middle_center);
    // No background colour: the text is drawn over the face, not in a box.
    canvas.setTextColor(numeralColor565);
    for (uint8_t i = 0; i < view.faceCount; i++) {
        const ScreenFace& face = view.faces[i];
        if (face.showsNumeral) canvas.drawNumber(face.numeral, face.numeralX, face.numeralY);
    }
}

#include "band_drawing.h"

namespace {

// The built-in 6 × 8 font doubled: "D20 20 20 20 = 60" is 204 px of the 240 px width. The size
// and the margin are what BAND_FONT_CHAR_WIDTH_PX and BAND_MARGIN_PX describe, so a change here
// is a change to what the band has room to say (ADR-0036) and the margin is read from there.
constexpr uint8_t TEXT_SIZE = 2;
// drawDieNumerals draws at whatever size is set, and the dice's numerals are the font's own size.
constexpr uint8_t DEFAULT_TEXT_SIZE = 1;
constexpr int CHIP_RADIUS_PX = 3;

} // namespace

void drawResultsBand(LGFX_Sprite& canvas, const ResultsBand& band, const BandColors& colors) {
    canvas.fillRect(0, 0, canvas.width(), BAND_HEIGHT_PX, colors.background565);
    canvas.setTextSize(TEXT_SIZE);
    canvas.setTextDatum(textdatum_t::middle_left);
    const int middleY = BAND_HEIGHT_PX / 2;
    int x = BAND_MARGIN_PX;
    for (uint8_t i = 0; i < band.segmentCount; i++) {
        const BandSegment& segment = band.segments[i];
        const int textWidth = canvas.textWidth(segment.text);
        if (segment.die == BAND_NO_DIE) {
            canvas.setTextColor(colors.text565);
            canvas.drawString(segment.text, x, middleY);
            x += textWidth + BAND_SEGMENT_GAP_PX;
            continue;
        }
        const int chipWidth = textWidth + 2 * BAND_CHIP_PADDING_PX;
        canvas.fillRoundRect(x, 1, chipWidth, BAND_HEIGHT_PX - 2, CHIP_RADIUS_PX, colors.dice565[segment.die]);
        canvas.setTextColor(colors.dieText565);
        canvas.drawString(segment.text, x + BAND_CHIP_PADDING_PX, middleY);
        x += chipWidth + BAND_SEGMENT_GAP_PX;
    }
    canvas.setTextSize(DEFAULT_TEXT_SIZE);
}

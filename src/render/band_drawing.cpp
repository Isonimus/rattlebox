#include "band_drawing.h"

namespace {

// The built-in 6 × 8 font doubled: "D20 20 20 20 = 60" is 204 px of the 240 px width.
constexpr uint8_t TEXT_SIZE = 2;
// drawDieNumerals draws at whatever size is set, and the dice's numerals are the font's own size.
constexpr uint8_t DEFAULT_TEXT_SIZE = 1;
constexpr int BAND_HEIGHT_PX = 20;
constexpr int MARGIN_PX = 4;
constexpr int CHIP_PADDING_PX = 3;
constexpr int CHIP_RADIUS_PX = 3;
constexpr int SEGMENT_GAP_PX = 6;

} // namespace

void drawResultsBand(LGFX_Sprite& canvas, const ResultsBand& band, const BandColors& colors) {
    canvas.fillRect(0, 0, canvas.width(), BAND_HEIGHT_PX, colors.background565);
    canvas.setTextSize(TEXT_SIZE);
    canvas.setTextDatum(textdatum_t::middle_left);
    const int middleY = BAND_HEIGHT_PX / 2;
    int x = MARGIN_PX;
    for (uint8_t i = 0; i < band.segmentCount; i++) {
        const BandSegment& segment = band.segments[i];
        const int textWidth = canvas.textWidth(segment.text);
        if (segment.die == BAND_NO_DIE) {
            canvas.setTextColor(colors.text565);
            canvas.drawString(segment.text, x, middleY);
            x += textWidth + SEGMENT_GAP_PX;
            continue;
        }
        const int chipWidth = textWidth + 2 * CHIP_PADDING_PX;
        canvas.fillRoundRect(x, 1, chipWidth, BAND_HEIGHT_PX - 2, CHIP_RADIUS_PX, colors.dice565[segment.die]);
        canvas.setTextColor(colors.dieText565);
        canvas.drawString(segment.text, x + CHIP_PADDING_PX, middleY);
        x += chipWidth + SEGMENT_GAP_PX;
    }
    canvas.setTextSize(DEFAULT_TEXT_SIZE);
}

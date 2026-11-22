#ifndef BAND_DRAWING_H
#define BAND_DRAWING_H

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "results_band.h"

/** @brief The colours the band is drawn in: its own, and each die's, so a result matches its die. */
struct BandColors {
    uint16_t background565;
    uint16_t text565;
    uint16_t dieText565;
    uint16_t dice565[MAX_DICE];
};

/** @brief The device half of the band: its own strip above the tray (ADR-0016), each die's result on a chip of its colour. */
void drawResultsBand(LGFX_Sprite& canvas, const ResultsBand& band, const BandColors& colors);

#endif // BAND_DRAWING_H

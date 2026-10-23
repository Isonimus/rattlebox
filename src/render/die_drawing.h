#ifndef DIE_DRAWING_H
#define DIE_DRAWING_H

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "die_view.h"

/**
 * @brief The device half of the renderer: hands a die's faces, then its numerals, to
 * LovyanGFX. Two calls rather than one so a caller can time the fill and the text apart.
 */
void drawDieFaces(LGFX_Sprite& canvas, const DieView& view);
void drawDieNumerals(LGFX_Sprite& canvas, const DieView& view, uint16_t numeralColor565);

#endif // DIE_DRAWING_H

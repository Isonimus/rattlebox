#ifndef ROLL_BAND_H
#define ROLL_BAND_H

#include <stdint.h>

#include "../render/results_band.h"
#include "die_catalog.h"

/**
 * @brief The band for a roll of @p type whose @p bodyCount bodies locked on @p numerals.
 *
 * It composes the three rules a locked band needs and that no single one of them owns: what each
 * body's numeral means, what each die scores from the bodies it threw, and how the band lays
 * those out (ADR-0034).
 *
 * It lives here and not in `results_band.cpp`, because the band draws text and must not know the
 * catalog; and here and not in `main.cpp`, because a band only the device can build is a band no
 * test can read.
 */
ResultsBand lockedBand(DieType type, const uint8_t* numerals, uint8_t bodyCount);

#endif // ROLL_BAND_H

#ifndef TRAY_LAYOUT_H
#define TRAY_LAYOUT_H

#include "../geometry/vec3.h"

/**
 * @brief Where a point of the tray, in millimetres, lies on the screen, in pixels: x across as the
 * tray's, y down from the screen's top where the tray's runs up from its near wall at the screen's
 * bottom edge, and z scaled alike for the die's own depth. The tray's far wall is the bottom edge
 * of the results band's strip (ADR-0016).
 */
Vec3 trayToScreenPx(Vec3 trayMm);

#endif // TRAY_LAYOUT_H

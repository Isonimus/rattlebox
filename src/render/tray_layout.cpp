#include "tray_layout.h"

#include "../physics/physics_config.h"

Vec3 trayToScreenPx(Vec3 trayMm) {
    return {trayMm.x * TRAY_PX_PER_MM, SCREEN_HEIGHT_PX - trayMm.y * TRAY_PX_PER_MM, trayMm.z * TRAY_PX_PER_MM};
}

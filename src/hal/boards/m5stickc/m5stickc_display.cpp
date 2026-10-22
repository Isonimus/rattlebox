#include "m5stickc_display.h"

bool M5StickCDisplay::begin() {
    if (!_tft.init()) return false;
    _tft.setRotation(1);
    _tft.setBrightness(255);
    _tft.fillScreen(TFT_BLACK);
    return true;
}

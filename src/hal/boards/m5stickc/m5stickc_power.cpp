#include "m5stickc_power.h"
#include <Arduino.h>

bool M5StickCPower::begin() {
    pinMode(HOLD_PIN, OUTPUT);
    digitalWrite(HOLD_PIN, HIGH);
    return true;
}

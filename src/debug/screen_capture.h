#pragma once
#include <Arduino.h>
#include <LovyanGFX.hpp>

// Answers a host's request for the frame on the display (esp32-dragon-radar slice 0011,
// carried over in slice 0003; scripts/capture_screen.py is the host side). Call once per loop, before
// anything draws, so `frame` still holds the whole frame the previous loop pushed.
void serviceScreenCapture(LGFX_Sprite& frame, Stream& port);

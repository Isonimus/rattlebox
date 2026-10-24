#pragma once
#include <Arduino.h>
#include <LovyanGFX.hpp>

// Answers a host's request for the frame on the display (esp32-dragon-radar slice 0011,
// carried over in slice 0003; scripts/capture_screen.py is the host side). Call once per loop, before
// anything draws, so `frame` still holds the whole frame the previous loop pushed.
//
// A byte other than the capture's own goes to `other`, when given, which returns whether it took
// the byte; the product's commands arrive this way (slice 0011). A byte nobody takes is counted.
using SerialByteHandler = bool (*)(int byte);
void serviceScreenCapture(LGFX_Sprite& frame, Stream& port, SerialByteHandler other = nullptr);

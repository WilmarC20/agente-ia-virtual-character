// FT6336 capacitive touch: read a screen-mapped point.
//
// LovyanGFX does NOT drive touch on this board — we read FT6336 over I2C.
// Mapping depends on DISPLAY_ROTATION in config.h (see TOUCH_* flags).
#pragma once

#include <Wire.h>
#include "config.h"

// Returns true while a finger is down; writes the mapped screen coords to sx/sy.
inline bool touchReadPoint(int screenW, int screenH, int &sx, int &sy) {
  Wire.beginTransmission(FT6336_I2C_ADDR);
  Wire.write(0x02);                                  // touch count register
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((uint8_t)FT6336_I2C_ADDR, (uint8_t)5);  // 0x02..0x06 auto-increment
  if (Wire.available() < 5) return false;

  uint8_t count = Wire.read() & 0x0F;   // 0x02
  uint8_t xh = Wire.read();             // 0x03 (X high nibble in bits 0-3)
  uint8_t xl = Wire.read();             // 0x04
  uint8_t yh = Wire.read();             // 0x05 (Y high nibble in bits 0-3)
  uint8_t yl = Wire.read();             // 0x06
  if (count == 0) return false;

  int rawX = ((xh & 0x0F) << 8) | xl;   // 0..239 (panel native width)
  int rawY = ((yh & 0x0F) << 8) | yl;   // 0..319 (panel native height)

#if TOUCH_SWAP_XY
  int x = rawY, y = rawX;               // landscape
#else
  int x = rawX, y = rawY;
#endif
#if TOUCH_INVERT_X
  x = (screenW - 1) - x;
#endif
#if TOUCH_INVERT_Y
  y = (screenH - 1) - y;
#endif
  if (x < 0) x = 0; else if (x >= screenW) x = screenW - 1;
  if (y < 0) y = 0; else if (y >= screenH) y = screenH - 1;
  sx = x; sy = y;

#if TOUCH_DEBUG
  Serial.printf("touch raw=(%d,%d) -> screen=(%d,%d)\n", rawX, rawY, sx, sy);
#endif
  return true;
}

// Blocks (briefly) until the finger lifts — used to avoid one tap registering twice.
inline void touchWaitRelease(int screenW, int screenH) {
  int sx, sy;
  uint32_t deadline = millis() + 1500;
  while (touchReadPoint(screenW, screenH, sx, sy) && (int32_t)(deadline - millis()) > 0) {
    delay(10);
  }
}

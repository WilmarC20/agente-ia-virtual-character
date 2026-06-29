// FT6336 capacitive touch: read a screen-mapped point.
//
// LovyanGFX does NOT drive touch on this board — we read FT6336 over I2C.
// Mapping follows g_activeDisplayRotation (see config.h).
#pragma once

#include <Wire.h>
#include "config.h"

// Returns true while a finger is down; writes the mapped screen coords to sx/sy.
inline bool touchReadPoint(int screenW, int screenH, int &sx, int &sy) {
  Wire.beginTransmission(FT6336_I2C_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((uint8_t)FT6336_I2C_ADDR, (uint8_t)5);
  if (Wire.available() < 5) return false;

  uint8_t count = Wire.read() & 0x0F;
  uint8_t xh = Wire.read();
  uint8_t xl = Wire.read();
  uint8_t yh = Wire.read();
  uint8_t yl = Wire.read();
  if (count == 0) return false;

  const int rawX = ((xh & 0x0F) << 8) | xl;
  const int rawY = ((yh & 0x0F) << 8) | yl;

  int x, y;
  const uint8_t rot = g_activeDisplayRotation;
  if (rot == 1) {
    x = rawY;
    y = rawX;
    x = (screenW - 1) - x;
  } else if (rot == 2) {
    x = rawX;
    y = rawY;
    x = (screenW - 1) - x;
    y = (screenH - 1) - y;
  } else {
    x = rawX;
    y = rawY;
  }
  if (x < 0) x = 0; else if (x >= screenW) x = screenW - 1;
  if (y < 0) y = 0; else if (y >= screenH) y = screenH - 1;
  sx = x;
  sy = y;

#if TOUCH_DEBUG
  Serial.printf("touch rot=%u raw=(%d,%d) -> screen=(%d,%d)\n", rot, rawX, rawY, sx, sy);
#endif
  return true;
}

inline void touchWaitRelease(int screenW, int screenH) {
  int sx, sy;
  uint32_t deadline = millis() + 1500;
  while (touchReadPoint(screenW, screenH, sx, sy) && (int32_t)(deadline - millis()) > 0) {
    delay(10);
  }
}

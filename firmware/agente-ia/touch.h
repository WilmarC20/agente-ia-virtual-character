// FT6336 capacitive touch: read a screen-mapped point.
//
// LovyanGFX does NOT drive touch on this board — we read FT6336 over I2C.
// Calibración por diseño (kitt, bender, …) en touch_calib.h / admin.
#pragma once

#include <Wire.h>
#include "config.h"
#include "touch_calib.h"

struct TouchRawSample {
  bool valid = false;
  bool down = false;
  int rawX = 0;
  int rawY = 0;
};

inline uint32_t g_touchFrameId = 0;
inline uint32_t g_touchSampleFrameId = 0;
inline TouchRawSample g_touchSample;

// Call once at the start of the main loop. All touch users in that loop share one I2C read.
inline void touchBeginFrame() {
  g_touchFrameId++;
  if (g_touchFrameId == 0) g_touchFrameId = 1;
}

// Modal screens (settings, etc.) run outside the main loop — drop stale per-frame cache.
inline void touchInvalidateCache() {
  g_touchFrameId = 0;
  g_touchSample.valid = false;
}

inline void mapTouchToScreen(int rawX, int rawY, int screenW, int screenH, const TouchCalib &calib,
                             int &sx, int &sy) {
  int x = rawX;
  int y = rawY;
  if (calib.swapXY) {
    x = rawY;
    y = rawX;
  }
  if (calib.invertX) x = (screenW - 1) - x;
  if (calib.invertY) y = (screenH - 1) - y;
  if (x < 0) x = 0;
  else if (x >= screenW) x = screenW - 1;
  if (y < 0) y = 0;
  else if (y >= screenH) y = screenH - 1;
  sx = x;
  sy = y;
}

inline bool touchReadRawFresh(int &rawX, int &rawY) {
  Wire.beginTransmission(FT6336_I2C_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((uint8_t)FT6336_I2C_ADDR, (uint8_t)5);
  if (Wire.available() < 5) return false;

  const uint8_t count = Wire.read() & 0x0F;
  const uint8_t xh = Wire.read();
  const uint8_t xl = Wire.read();
  const uint8_t yh = Wire.read();
  const uint8_t yl = Wire.read();
  if (count == 0) return false;

  rawX = ((xh & 0x0F) << 8) | xl;
  rawY = ((yh & 0x0F) << 8) | yl;
  return true;
}

inline bool touchReadRaw(int &rawX, int &rawY) {
  if (g_touchFrameId != 0 && g_touchSampleFrameId == g_touchFrameId && g_touchSample.valid) {
    rawX = g_touchSample.rawX;
    rawY = g_touchSample.rawY;
    return g_touchSample.down;
  }

  const bool down = touchReadRawFresh(rawX, rawY);
  if (g_touchFrameId != 0) {
    g_touchSampleFrameId = g_touchFrameId;
    g_touchSample.valid = true;
    g_touchSample.down = down;
    g_touchSample.rawX = rawX;
    g_touchSample.rawY = rawY;
  }
  return down;
}

inline bool touchReadPoint(int screenW, int screenH, int &sx, int &sy) {
  int rawX, rawY;
  if (!touchReadRaw(rawX, rawY)) return false;
  mapTouchToScreen(rawX, rawY, screenW, screenH, touchCalibActive(), sx, sy);

#if TOUCH_DEBUG
  Serial.printf("touch [%s] raw=(%d,%d) -> screen=(%d,%d)\n", touchCalibActivePresentation(),
                rawX, rawY, sx, sy);
#endif
  return true;
}

inline bool touchReadPointRaw(int screenW, int screenH, int &sx, int &sy, int &rawX,
                              int &rawY) {
  if (!touchReadRaw(rawX, rawY)) return false;
  mapTouchToScreen(rawX, rawY, screenW, screenH, touchCalibActive(), sx, sy);
  return true;
}

inline void touchWaitRelease(int screenW, int screenH) {
  touchInvalidateCache();
  int sx, sy;
  uint32_t deadline = millis() + 1500;
  while (touchReadPoint(screenW, screenH, sx, sy) && (int32_t)(deadline - millis()) > 0) {
    touchInvalidateCache();
    delay(10);
  }
  touchInvalidateCache();
}

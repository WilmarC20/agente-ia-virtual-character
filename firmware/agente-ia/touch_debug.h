#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "touch.h"
#include "touch_calib.h"

// Marca táctil encima del frame ya dibujado (llamar después de pushSprite / draw).
inline void touchDebugStamp(lgfx::LGFX_Device &gfx, int sx, int sy, int rawX, int rawY) {
  static uint32_t lastLog = 0;
  if (millis() - lastLog > 150) {
    lastLog = millis();
    Serial.printf("touch dbg [%s] raw=(%d,%d) scr=(%d,%d) %dx%d\n",
                  touchCalibActivePresentation(), rawX, rawY, sx, sy, gfx.width(), gfx.height());
  }

  gfx.drawCircle(sx, sy, 10, TFT_YELLOW);
  gfx.drawLine(sx - 14, sy, sx + 14, sy, TFT_YELLOW);
  gfx.drawLine(sx, sy - 14, sx, sy + 14, TFT_YELLOW);
  gfx.fillCircle(sx, sy, 3, TFT_RED);

  char buf[56];
  snprintf(buf, sizeof(buf), "%d,%d", sx, sy);
  gfx.setFont(&fonts::Font2);
  gfx.setTextColor(TFT_WHITE, TFT_BLACK);
  int tx = sx + 12;
  int ty = sy + 12;
  if (tx > (int)gfx.width() - 76) tx = sx - 76;
  if (ty > (int)gfx.height() - 40) ty = sy - 40;
  if (tx < 0) tx = 0;
  if (ty < 10) ty = 10;
  gfx.fillRect(tx - 1, ty - 10, 74, 30, TFT_BLACK);
  gfx.setCursor(tx, ty);
  gfx.print(buf);
  snprintf(buf, sizeof(buf), "r%d,%d", rawX, rawY);
  gfx.setCursor(tx, ty + 12);
  gfx.print(buf);
}

inline void touchDebugStampIfTouching(lgfx::LGFX_Device &gfx) {
  if (!touchCalibActive().showDebug) return;
  int sx, sy, rawX, rawY;
  if (!touchReadPointRaw(gfx.width(), gfx.height(), sx, sy, rawX, rawY)) return;
  touchDebugStamp(gfx, sx, sy, rawX, rawY);
}

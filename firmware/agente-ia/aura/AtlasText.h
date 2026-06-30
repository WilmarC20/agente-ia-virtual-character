#pragma once

#include <LovyanGFX.hpp>
#include "../font_kitt_extended.h"

// Texto híbrido: labels fijos vía fuente bitmap estirada (Fase 1).
// Futuro: atlas RGB565 desde SPIFFS para labels estáticos + glyph atlas dinámico.
class AtlasText {
 public:
  static void drawCentered(lgfx::LGFX_Sprite &c, int x, int y, int w, int h,
                           uint16_t fg, uint16_t bg, const char *text) {
    c.setFont(&KittFontExtended);
    c.setTextColor(fg, bg);
    c.setTextSize(1.0f, 1.0f);
    const float baseW = (float)c.textWidth(text);
    float sx = (baseW > 0) ? (0.86f * (float)w / baseW) : 1.0f;
    if (sx > 1.6f) sx = 1.6f;
    if (sx < 0.45f) sx = 0.45f;
    float sy = 1.0f;
    const float baseH = (float)c.fontHeight();
    if (baseH * sy > (float)(h - 2)) sy = (float)(h - 2) / baseH;
    c.setTextSize(sx, sy);
    c.setTextDatum(textdatum_t::middle_center);
    c.drawString(text, x + w / 2, y + h / 2 + 1);
    c.setTextDatum(textdatum_t::top_left);
    c.setTextSize(1.0f, 1.0f);
  }

  static void drawTwoLine(lgfx::LGFX_Sprite &c, int x, int y, int w, int h,
                          uint16_t fg, uint16_t bg, const char *l1, const char *l2) {
    c.setFont(&KittFontExtended);
    c.setTextColor(fg, bg);
    c.setTextSize(1.0f, 1.0f);
    const float baseH = (float)c.fontHeight();
    float sy = (float)((h / 2) - 1) / baseH;
    if (sy > 0.95f) sy = 0.95f;
    if (sy < 0.35f) sy = 0.35f;
    const int lineH = (int)(baseH * sy);
    const int cy = y + h / 2;
    c.setTextDatum(textdatum_t::middle_center);
    auto line = [&](const char *s, int ly) {
      c.setTextSize(1.0f, 1.0f);
      const float bw = (float)c.textWidth(s);
      float sx = (bw > 0) ? (0.86f * (float)w / bw) : 1.0f;
      if (sx > 1.6f) sx = 1.6f;
      if (sx < 0.45f) sx = 0.45f;
      c.setTextSize(sx, sy);
      c.drawString(s, x + w / 2, ly);
    };
    line(l1, cy - lineH / 2);
    line(l2, cy + lineH / 2);
    c.setTextDatum(textdatum_t::top_left);
    c.setTextSize(1.0f, 1.0f);
  }
};

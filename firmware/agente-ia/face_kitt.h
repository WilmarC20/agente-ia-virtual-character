// Tablero KITT — copia 1:1 de la imagen de referencia, recalculada para 240×320.
// Imagen original 768×1024 (3:4). Factor de escala = 240/768 = 0.3125.
// Todas las coordenadas derivan de medir la imagen y multiplicar por 0.3125.
#pragma once

#include <math.h>
#include <LovyanGFX.hpp>
#include "font_kitt_extended.h"  // Michroma (alt. libre de Microgramma/Eurostile) bold

struct KittDrawCtx {
  int emotion = 0;
  bool talking = false;
  bool listening = false;
  float mouthAmp = 0.0f;
  uint32_t nowMs = 0;
  const float *vibingBands = nullptr;
  int vibingBandCount = 0;
};

namespace KittUi {

// ----- Lienzo (portrait nativo) -----
static constexpr int CANVAS_W = 240;
static constexpr int CANVAS_H = 320;

// ----- Colores sólidos (de la imagen) -----
static constexpr uint16_t BG = 0x0000;          // #000000
static constexpr uint16_t RED = 0xF800;         // #FF0000 (POWER, bloques)
static constexpr uint16_t ORANGE = 0xFC60;      // #FF8C00 (MIN RPM, AUTO CRUISE)
static constexpr uint16_t YELLOW = 0xFFE0;      // #FFFF00 (FUEL ON, IGNITORS, AIR/OIL/S1/S2, NORMAL)
static constexpr uint16_t BLUE = 0x041F;        // #0080FF (PURSUIT)
static constexpr uint16_t PILL_ORANGE = 0xFB00; // #FF6000 (P1–P4)
static constexpr uint16_t SEG_OFF = 0x0000;     // bloques apagados = negro (no aparecen en la imagen)

// ----- Barras superiores (4 horizontales) -----
static constexpr int TOP_X = 73;
static constexpr int TOP_W = 94;
static constexpr int TOP_H = 16;
static constexpr int TOP_PITCH = 23;   // separación vertical entre tops
static constexpr int TOP_Y0 = 5;
static constexpr int TOP_RADIUS = 4;

// ----- Óvalos laterales (4 por lado) -----
static constexpr int OVAL_W = 44;
static constexpr int OVAL_H = 26;
static constexpr int OVAL_L_X = 12;
static constexpr int OVAL_R_X = 184;   // 240 - 12 - 44
static constexpr int OVAL_Y0 = 118;
static constexpr int OVAL_PITCH = 38;

// ----- Columnas centrales (3, bloques separados) -----
static constexpr int SEG_W = 18;
static constexpr int SEG_H = 4;
static constexpr int SEG_GAP = 2;
static constexpr int COL_CENTER_Y = 158;
static constexpr int COL_CX_LEFT = 90;
static constexpr int COL_CX_MID = 120;
static constexpr int COL_CX_RIGHT = 150;
static constexpr int COL_SEG_SIDE = 15;   // columnas externas
static constexpr int COL_SEG_MID = 19;    // columna central (más alta)

// ----- Barras inferiores (3 horizontales) -----
static constexpr int BOT_X = 73;
static constexpr int BOT_W = 94;
static constexpr int BOT_H = 24;
static constexpr int BOT_PITCH = 28;
static constexpr int BOT_Y0 = 230;
static constexpr int BOT_RADIUS = 8;

// Tipografía: negrita fuerte y "extended" (estirada horizontalmente) al estilo
// Eurostile Bold Extended de KITT. Usamos FreeSansBold (la más gruesa disponible
// en LovyanGFX) y la ensanchamos por software para llenar el ancho del botón.
inline void drawCenteredLabel(lgfx::LGFX_Sprite &c, int x, int y, int w, int h,
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

inline void drawTwoLineLabel(lgfx::LGFX_Sprite &c, int x, int y, int w, int h,
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

inline void drawTopBars(lgfx::LGFX_Sprite &c) {
  struct Bar { uint16_t col; const char *label; };
  const Bar bars[] = {
      {RED, "POWER"},
      {ORANGE, "MIN RPM"},
      {YELLOW, "FUEL ON"},
      {YELLOW, "IGNITORS"},
  };
  for (int i = 0; i < 4; i++) {
    const int y = TOP_Y0 + i * TOP_PITCH;
    c.fillRoundRect(TOP_X, y, TOP_W, TOP_H, TOP_RADIUS, bars[i].col);
    drawCenteredLabel(c, TOP_X, y, TOP_W, TOP_H, TFT_BLACK, bars[i].col, bars[i].label);
  }
}

inline void drawOval(lgfx::LGFX_Sprite &c, int x, int y, uint16_t col, const char *label) {
  c.fillRoundRect(x, y, OVAL_W, OVAL_H, OVAL_H / 2, col);
  drawCenteredLabel(c, x, y, OVAL_W, OVAL_H, TFT_BLACK, col, label);
}

inline void drawSideOvals(lgfx::LGFX_Sprite &c) {
  static const char *kLeft[] = {"AIR", "OIL", "P1", "P2"};
  static const char *kRight[] = {"S1", "S2", "P3", "P4"};
  for (int i = 0; i < 4; i++) {
    const int y = OVAL_Y0 + i * OVAL_PITCH;
    const uint16_t col = (i < 2) ? YELLOW : PILL_ORANGE;
    drawOval(c, OVAL_L_X, y, col, kLeft[i]);
    drawOval(c, OVAL_R_X, y, col, kRight[i]);
  }
}

inline void drawBottomBars(lgfx::LGFX_Sprite &c) {
  // AUTO CRUISE y NORMAL CRUISE en dos líneas; PURSUIT en una.
  const int yAuto = BOT_Y0;
  const int yNormal = BOT_Y0 + BOT_PITCH;
  const int yPursuit = BOT_Y0 + 2 * BOT_PITCH;
  c.fillRoundRect(BOT_X, yAuto, BOT_W, BOT_H, BOT_RADIUS, ORANGE);
  drawTwoLineLabel(c, BOT_X, yAuto, BOT_W, BOT_H, TFT_BLACK, ORANGE, "AUTO", "CRUISE");
  c.fillRoundRect(BOT_X, yNormal, BOT_W, BOT_H, BOT_RADIUS, YELLOW);
  drawTwoLineLabel(c, BOT_X, yNormal, BOT_W, BOT_H, TFT_BLACK, YELLOW, "NORMAL", "CRUISE");
  c.fillRoundRect(BOT_X, yPursuit, BOT_W, BOT_H, BOT_RADIUS, BLUE);
  drawCenteredLabel(c, BOT_X, yPursuit, BOT_W, BOT_H, TFT_BLACK, BLUE, "PURSUIT");
}

// Cuántos bloques encender (simétrico desde el centro de la columna).
inline int litCenterOut(int maxSeg, float level, float wobble) {
  int n = (int)lroundf((float)maxSeg * level + wobble);
  if (n < 0) n = 0;
  if (n > maxSeg) n = maxSeg;
  return n;
}

inline void drawColumn(lgfx::LGFX_Sprite &c, int cx, int maxSeg, int lit) {
  const int totalH = maxSeg * SEG_H + (maxSeg - 1) * SEG_GAP;
  const int topY = COL_CENTER_Y - totalH / 2;
  const int mid = (maxSeg - 1) / 2;
  const int firstLit = (lit > 0) ? (mid - (lit - 1) / 2) : maxSeg;
  const int lastLit = (lit > 0) ? (firstLit + lit - 1) : -1;
  const int x = cx - SEG_W / 2;
  for (int i = 0; i < maxSeg; i++) {
    const int y = topY + i * (SEG_H + SEG_GAP);
    const bool on = (lit > 0) && (i >= firstLit) && (i <= lastLit);
    c.fillRect(x, y, SEG_W, SEG_H, on ? RED : SEG_OFF);
  }
}

inline void drawModulator(lgfx::LGFX_Sprite &c, const KittDrawCtx &ctx) {
  const float t = ctx.nowMs * 0.001f;
  const bool hasVibing = ctx.vibingBands && ctx.vibingBandCount >= 6;
  const bool active = ctx.talking || ctx.listening || hasVibing;

  struct Col { int cx; int maxSeg; float phase; };
  const Col cols[] = {
      {COL_CX_LEFT, COL_SEG_SIDE, 0.0f},
      {COL_CX_MID, COL_SEG_MID, 1.0f},
      {COL_CX_RIGHT, COL_SEG_SIDE, 2.1f},
  };

  // En silencio: columnas fijas y completas (idénticas a la imagen).
  if (!active) {
    for (const auto &col : cols) drawColumn(c, col.cx, col.maxSeg, col.maxSeg);
    return;
  }

  if (hasVibing) {
    const int per = ctx.vibingBandCount / 3;
    for (int i = 0; i < 3; i++) {
      float lvl = 0.0f;
      const int b0 = i * per;
      for (int b = b0; b < b0 + per && b < ctx.vibingBandCount; b++) lvl += ctx.vibingBands[b];
      lvl = lvl / (per * 250.0f);
      if (lvl > 1.0f) lvl = 1.0f;
      const float wob = sinf(t * 11.0f + cols[i].phase) * 1.8f;
      drawColumn(c, cols[i].cx, cols[i].maxSeg, litCenterOut(cols[i].maxSeg, lvl, wob));
    }
    return;
  }

  // Amplitud REAL del sonido (TTS por RMS de reproducción, escucha por micrófono).
  float amp = ctx.mouthAmp / 100.0f;
  if (amp < 0.0f) amp = 0.0f;
  if (amp > 1.0f) amp = 1.0f;
  amp = powf(amp, 0.7f);

  for (const auto &col : cols) {
    const float wob = sinf(t * 9.0f + col.phase) * (0.4f + 1.6f * amp);
    drawColumn(c, col.cx, col.maxSeg, litCenterOut(col.maxSeg, amp, wob));
  }
}

inline void drawDashboard(lgfx::LGFX_Sprite &c, const KittDrawCtx &ctx) {
  c.fillSprite(BG);
  drawTopBars(c);
  drawSideOvals(c);
  drawModulator(c, ctx);
  drawBottomBars(c);
}

}  // namespace KittUi

inline void drawKittDashboard(lgfx::LGFX_Sprite &canvas, const KittDrawCtx &ctx) {
  KittUi::drawDashboard(canvas, ctx);
}

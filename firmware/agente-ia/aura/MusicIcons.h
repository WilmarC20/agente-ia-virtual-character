#pragma once

#include <LovyanGFX.hpp>
#include <algorithm>

// Iconos procedurales para controles de música (sin texto, caben en botones KITT pequeños).
namespace MusicIcons {

inline int clampSize(int w, int h, int margin = 6) {
  const int m = std::min(w, h) - margin;
  return m < 8 ? 8 : m;
}

template <typename Gfx>
inline void drawStop(Gfx &c, int cx, int cy, int size, uint16_t color) {
  const int s = std::max(4, size / 3);
  c.fillRect(cx - s, cy - s, 2 * s, 2 * s, color);
}

template <typename Gfx>
inline void drawPrev(Gfx &c, int cx, int cy, int size, uint16_t color) {
  const int h = std::max(4, size / 2);
  const int w = std::max(4, size / 2);
  c.fillTriangle(cx + w / 4, cy - h, cx + w / 4, cy + h, cx - w, cy, color);
  c.fillRect(cx + w / 4 + 1, cy - h + 2, std::max(2, w / 5), 2 * h - 4, color);
}

template <typename Gfx>
inline void drawNext(Gfx &c, int cx, int cy, int size, uint16_t color) {
  const int h = std::max(4, size / 2);
  const int w = std::max(4, size / 2);
  c.fillTriangle(cx - w / 4, cy - h, cx - w / 4, cy + h, cx + w, cy, color);
  c.fillRect(cx - w / 4 - std::max(2, w / 5) - 1, cy - h + 2, std::max(2, w / 5), 2 * h - 4, color);
}

template <typename Gfx>
inline void drawMinus(Gfx &c, int cx, int cy, int size, uint16_t color) {
  const int w = std::max(6, size);
  c.fillRoundRect(cx - w / 2, cy - 2, w, 4, 2, color);
}

template <typename Gfx>
inline void drawPlus(Gfx &c, int cx, int cy, int size, uint16_t color) {
  const int w = std::max(6, size);
  c.fillRoundRect(cx - w / 2, cy - 2, w, 4, 2, color);
  c.fillRoundRect(cx - 2, cy - w / 2, 4, w, 2, color);
}

template <typename Gfx>
inline void drawLoop(Gfx &c, int cx, int cy, int size, uint16_t color) {
  const int r = std::max(3, size / 4);
  c.drawCircle(cx, cy, r, color);
  c.drawCircle(cx, cy, r - 2, color);
  c.fillTriangle(cx + r - 1, cy - 2, cx + r + 4, cy, cx + r - 1, cy + 2, color);
}

template <typename Gfx>
inline void drawNote(Gfx &c, int cx, int cy, int size, uint16_t color) {
  const int h = std::max(6, size / 2);
  c.fillCircle(cx - h / 4, cy + h / 3, h / 4, color);
  c.fillRect(cx - h / 4 + h / 5, cy - h, std::max(2, h / 6), h + h / 3, color);
}

enum class Kind : uint8_t { Stop, Prev, Next, Minus, Plus, Loop, Note };

template <typename Gfx>
inline void drawKind(Gfx &c, Kind k, int cx, int cy, int size, uint16_t color) {
  switch (k) {
    case Kind::Stop: drawStop(c, cx, cy, size, color); break;
    case Kind::Prev: drawPrev(c, cx, cy, size, color); break;
    case Kind::Next: drawNext(c, cx, cy, size, color); break;
    case Kind::Minus: drawMinus(c, cx, cy, size, color); break;
    case Kind::Plus: drawPlus(c, cx, cy, size, color); break;
    case Kind::Loop: drawLoop(c, cx, cy, size, color); break;
    case Kind::Note: drawNote(c, cx, cy, size, color); break;
  }
}

template <typename Gfx>
inline void drawButton(Gfx &c, int x, int y, int w, int h, int radius, uint16_t bg, Kind icon,
                       uint16_t fg, bool highlight = false) {
  c.fillRoundRect(x, y, w, h, radius, bg);
  if (highlight) c.drawRoundRect(x, y, w, h, radius, 0xFFFF);
  const int cx = x + w / 2;
  const int cy = y + h / 2;
  const int margin = (h > 36) ? 12 : 6;
  drawKind(c, icon, cx, cy, clampSize(w, h, margin), fg);
}

}  // namespace MusicIcons

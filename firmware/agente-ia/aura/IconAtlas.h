#pragma once

#include <LovyanGFX.hpp>

// Atlas de iconos/glyphs RGB565 — carga desde SPIFFS (labels_atlas.bin + labels_atlas.json).
class IconAtlas {
 public:
  bool load(const char * /*binPath*/, const char * /*jsonPath*/) { return false; }

  void drawSprite(lgfx::LGFX_Sprite & /*c*/, const char * /*id*/, int /*x*/, int /*y*/) {}

  bool hasGlyph(const char * /*ch*/) const { return false; }

  void drawText(lgfx::LGFX_Sprite &c, int x, int y, const char *text, uint16_t fg, uint16_t bg) {
    c.setTextColor(fg, bg);
    c.setCursor(x, y);
    c.print(text);
  }
};

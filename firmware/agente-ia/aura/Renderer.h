#pragma once

#include <LovyanGFX.hpp>

// Único punto de acceso a primitivas gráficas sobre sprites (AURA).
class AuraRenderer {
 public:
  void bind(lgfx::LGFX_Sprite *sprite) { _sprite = sprite; }

  void fillBackground(uint16_t color) {
    if (_sprite) _sprite->fillSprite(color);
  }

  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
    if (_sprite) _sprite->fillRoundRect(x, y, w, h, r, color);
  }

  void fillRect(int x, int y, int w, int h, uint16_t color) {
    if (_sprite) _sprite->fillRect(x, y, w, h, color);
  }

  lgfx::LGFX_Sprite *sprite() { return _sprite; }

 private:
  lgfx::LGFX_Sprite *_sprite = nullptr;
};

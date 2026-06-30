#pragma once

#include <LovyanGFX.hpp>
#include "../AtlasText.h"
#include "../Renderer.h"
#include "../ThemeManager.h"

// Tarjeta de estado (título + subtítulo) — esquina superior en escenas overlay.
class StatusCardWidget {
 public:
  void draw(lgfx::LGFX_Sprite &c, int x, int y, int w, int h,
            const AuraTheme &theme, const char *title, const char *subtitle) {
    AuraRenderer r;
    r.bind(&c);
    r.fillRoundRect(x, y, w, h, 6, theme.blue);
    AtlasText::drawCentered(c, x, y, w, h / 2, theme.labelFg, theme.blue, title);
    if (subtitle && subtitle[0]) {
      AtlasText::drawCentered(c, x, y + h / 2, w, h / 2, theme.labelFg, theme.blue, subtitle);
    }
  }
};

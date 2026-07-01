#pragma once

#include <LovyanGFX.hpp>
#include "../AtlasText.h"
#include "../MusicIcons.h"
#include "../Renderer.h"
#include "../ThemeManager.h"

// Tira de título musical — icono + nombre de pista (sin etiqueta "MUSIC").
class StatusCardWidget {
 public:
  void draw(lgfx::LGFX_Sprite &c, int x, int y, int w, int h,
            const AuraTheme &theme, const char *title, const char *subtitle) {
    (void)title;
    AuraRenderer r;
    r.bind(&c);
    r.fillRoundRect(x, y, w, h, 6, theme.blue);

    const int iconPad = 6;
    MusicIcons::drawNote(c, x + iconPad + 6, y + h / 2, 14, theme.labelFg);

    if (subtitle && subtitle[0]) {
      c.setFont(&fonts::DejaVu12);
      c.setTextColor(theme.labelFg, theme.blue);
      c.setTextWrap(false);
      String line = subtitle;
      const int maxPx = w - 28;
      while (line.length() > 0 && c.textWidth(line) > maxPx) {
        if (line.length() <= 4) break;
        line = line.substring(0, line.length() - 4) + "...";
      }
      c.setCursor(x + 24, y + (h - c.fontHeight()) / 2);
      c.print(line);
    }
  }
};

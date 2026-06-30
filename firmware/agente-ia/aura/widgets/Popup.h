#pragma once

#include <LovyanGFX.hpp>
#include "../AtlasText.h"
#include "../Renderer.h"
#include "../ThemeManager.h"

class PopupWidget {
 public:
  bool active() const { return _untilMs > 0 && millis() < _untilMs; }

  void show(const char *msg, uint32_t durationMs = 3000) {
    _msg = msg ? msg : "";
    _untilMs = millis() + durationMs;
  }

  void draw(lgfx::LGFX_Sprite &c, int cw, int ch, const AuraTheme &theme) {
    if (!active() || _msg.isEmpty()) return;
    const int w = cw - 24;
    const int h = 48;
    const int x = 12;
    const int y = ch / 2 - h / 2;
    AuraRenderer r;
    r.bind(&c);
    r.fillRoundRect(x, y, w, h, 8, theme.orange);
    AtlasText::drawCentered(c, x, y, w, h, theme.labelFg, theme.orange, _msg.c_str());
  }

 private:
  String _msg;
  uint32_t _untilMs = 0;
};

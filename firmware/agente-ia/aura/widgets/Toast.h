#pragma once

#include <LovyanGFX.hpp>
#include "../AtlasText.h"
#include "../Renderer.h"
#include "../ThemeManager.h"

class ToastWidget {
 public:
  void push(const char *msg, uint32_t durationMs = 2200) {
    _msg = msg ? msg : "";
    _untilMs = millis() + durationMs;
  }

  void draw(lgfx::LGFX_Sprite &c, int cw, const AuraTheme &theme) {
    if (_untilMs == 0 || millis() > _untilMs || _msg.isEmpty()) return;
    const int h = 22;
    const int y = 4;
    AuraRenderer r;
    r.bind(&c);
    r.fillRoundRect(8, y, cw - 16, h, 4, theme.yellow);
    AtlasText::drawCentered(c, 8, y, cw - 16, h, theme.labelFg, theme.yellow, _msg.c_str());
  }

 private:
  String _msg;
  uint32_t _untilMs = 0;
};

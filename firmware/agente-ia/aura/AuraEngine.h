#pragma once

#include <LovyanGFX.hpp>
#include "ThemeManager.h"
#include "LayoutManager.h"
#include "AnimationEngine.h"
#include "widgets/KittDashboard.h"
#include "../face_kitt.h"

class AuraEngine {
 public:
  AuraEngine() {
    _theme.loadKittDefaults();
    _layout.loadKittDefaults();
  }

  void drawKitt(lgfx::LGFX_Sprite &canvas, const KittDrawCtx &ctx) {
    _dashboard.draw(canvas, ctx, _theme, _layout, _anim);
  }

  bool hitSettingsP4(int sx, int sy) const {
    AuraRect z = _layout.hitZoneSettingsP4();
    return sx >= z.x && sx <= z.x + z.w && sy >= z.y && sy <= z.y + z.h;
  }

  int canvasW() const { return _layout.layout().canvasW; }
  int canvasH() const { return _layout.layout().canvasH; }

 private:
  ThemeManager _theme;
  LayoutManager _layout;
  AnimationEngine _anim;
  KittDashboard _dashboard;
};

inline AuraEngine g_aura;

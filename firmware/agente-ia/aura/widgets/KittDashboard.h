#pragma once

#include "../Renderer.h"
#include "../AtlasText.h"
#include "../AnimationEngine.h"
#include "../../face_kitt.h"

// Escena KITT — paridad 1:1 con face_kitt.h vía AURA.
class KittDashboard {
 public:
  void draw(lgfx::LGFX_Sprite &canvas, const KittDrawCtx &ctx,
            ThemeManager &themeMgr, LayoutManager &layoutMgr, AnimationEngine &anim) {
    AuraRenderer r;
    r.bind(&canvas);
    const AuraTheme &T = themeMgr.theme();
    const AuraLayout &L = layoutMgr.layout();

    r.fillBackground(T.background);

    struct Bar { uint16_t col; const char *label; };
    const Bar tops[] = {
        {T.red, "POWER"}, {T.orange, "MIN RPM"}, {T.yellow, "FUEL ON"}, {T.yellow, "IGNITORS"},
    };
    for (int i = 0; i < 4; i++) {
      AuraRect rect = layoutMgr.topBar(i);
      r.fillRoundRect(rect.x, rect.y, rect.w, rect.h, L.topRadius, tops[i].col);
      AtlasText::drawCentered(canvas, rect.x, rect.y, rect.w, rect.h, T.labelFg, tops[i].col, tops[i].label);
    }

    static const char *kLeft[] = {"AIR", "OIL", "P1", "P2"};
    static const char *kRight[] = {"S1", "S2", "P3", "P4"};
    for (int i = 0; i < 4; i++) {
      const uint16_t col = (i < 2) ? T.yellow : T.pillOrange;
      AuraRect l = layoutMgr.ovalLeft(i);
      AuraRect rt = layoutMgr.ovalRight(i);
      r.fillRoundRect(l.x, l.y, l.w, l.h, l.h / 2, col);
      AtlasText::drawCentered(canvas, l.x, l.y, l.w, l.h, T.labelFg, col, kLeft[i]);
      r.fillRoundRect(rt.x, rt.y, rt.w, rt.h, rt.h / 2, col);
      AtlasText::drawCentered(canvas, rt.x, rt.y, rt.w, rt.h, T.labelFg, col, kRight[i]);
    }

    anim.drawModulator(r, layoutMgr, T, ctx);

    AuraRect autoBar = layoutMgr.bottomBar(0);
    AuraRect normalBar = layoutMgr.bottomBar(1);
    AuraRect pursuitBar = layoutMgr.bottomBar(2);
    r.fillRoundRect(autoBar.x, autoBar.y, autoBar.w, autoBar.h, L.botRadius, T.orange);
    AtlasText::drawTwoLine(canvas, autoBar.x, autoBar.y, autoBar.w, autoBar.h, T.labelFg, T.orange, "AUTO", "CRUISE");
    r.fillRoundRect(normalBar.x, normalBar.y, normalBar.w, normalBar.h, L.botRadius, T.yellow);
    AtlasText::drawTwoLine(canvas, normalBar.x, normalBar.y, normalBar.w, normalBar.h, T.labelFg, T.yellow, "NORMAL", "CRUISE");
    r.fillRoundRect(pursuitBar.x, pursuitBar.y, pursuitBar.w, pursuitBar.h, L.botRadius, T.blue);
    AtlasText::drawCentered(canvas, pursuitBar.x, pursuitBar.y, pursuitBar.w, pursuitBar.h, T.labelFg, T.blue, "PURSUIT");
  }
};

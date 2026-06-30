#pragma once

#include <math.h>
#include "Renderer.h"
#include "LayoutManager.h"
#include "ThemeManager.h"
#include "../face_kitt.h"

class AnimationEngine {
 public:
  static int litCenterOut(int maxSeg, float level, float wobble) {
    int n = (int)lroundf((float)maxSeg * level + wobble);
    if (n < 0) n = 0;
    if (n > maxSeg) n = maxSeg;
    return n;
  }

  void drawModulator(AuraRenderer &r, const LayoutManager &layout, const AuraTheme &theme,
                     const KittDrawCtx &ctx) {
    lgfx::LGFX_Sprite *c = r.sprite();
    if (!c) return;

    const AuraLayout &L = layout.layout();
    const float t = ctx.nowMs * 0.001f;
    const bool hasVibing = ctx.vibingBands && ctx.vibingBandCount >= 6;
    const bool active = ctx.talking || ctx.listening || hasVibing;

    struct Col { int cx; int maxSeg; float phase; };
    const Col cols[] = {
        {L.colCxLeft, L.colSegSide, 0.0f},
        {L.colCxMid, L.colSegMid, 1.0f},
        {L.colCxRight, L.colSegSide, 2.1f},
    };

    auto drawColumn = [&](int cx, int maxSeg, int lit) {
      const int totalH = maxSeg * L.segH + (maxSeg - 1) * L.segGap;
      const int topY = L.colCenterY - totalH / 2;
      const int mid = (maxSeg - 1) / 2;
      const int firstLit = (lit > 0) ? (mid - (lit - 1) / 2) : maxSeg;
      const int lastLit = (lit > 0) ? (firstLit + lit - 1) : -1;
      const int x = cx - L.segW / 2;
      for (int i = 0; i < maxSeg; i++) {
        const int y = topY + i * (L.segH + L.segGap);
        const bool on = (lit > 0) && (i >= firstLit) && (i <= lastLit);
        r.fillRect(x, y, L.segW, L.segH, on ? theme.red : theme.segmentOff);
      }
    };

    if (!active) {
      for (const auto &col : cols) drawColumn(col.cx, col.maxSeg, col.maxSeg);
      return;
    }

    float amp = ctx.mouthAmp / 100.0f;
    if (ctx.talking) {
      amp = max(amp, 0.45f + 0.4f * sinf(t * 14.0f));
    } else if (ctx.listening) {
      amp = 0.35f + 0.18f * sinf(t * 5.0f);
    } else {
      amp = max(amp, 0.25f + 0.1f * sinf(t * 2.0f));
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
        drawColumn(cols[i].cx, cols[i].maxSeg, litCenterOut(cols[i].maxSeg, lvl, wob));
      }
      return;
    }

    for (const auto &col : cols) {
      const float level = 0.25f + 0.75f * amp;
      const float wob = sinf(t * 12.0f + col.phase) * 2.2f;
      drawColumn(col.cx, col.maxSeg, litCenterOut(col.maxSeg, level, wob));
    }
  }
};

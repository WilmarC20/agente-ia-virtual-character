#pragma once

#include "../Renderer.h"
#include "../LayoutManager.h"
#include "../ThemeManager.h"
#include "../AnimationEngine.h"
#include "../face_kitt.h"

// Indicador de voz — columnas centrales del tablero KITT.
class VoiceIndicatorWidget {
 public:
  void draw(AuraRenderer &r, const LayoutManager &layout, const AuraTheme &theme,
            AnimationEngine &anim, const KittDrawCtx &ctx) {
    anim.drawModulator(r, layout, theme, ctx);
  }
};

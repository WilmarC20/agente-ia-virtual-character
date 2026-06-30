#pragma once

#include <LovyanGFX.hpp>
#include "AuraEngine.h"
#include "../face_kitt.h"

inline void auraDrawKitt(lgfx::LGFX_Sprite &canvas, const KittDrawCtx &ctx) {
  g_aura.drawKitt(canvas, ctx);
}

inline bool auraHitSettingsP4(int sx, int sy) {
  return g_aura.hitSettingsP4(sx, sy);
}

inline void auraBindFace(class Face *face) {
  g_aura.bindFace(face);
}

inline void auraApplyEmotion(const char *emotion, float intensity, uint32_t recoveryMs = 8000) {
  g_aura.applyEmotionState(emotion, intensity, recoveryMs);
}

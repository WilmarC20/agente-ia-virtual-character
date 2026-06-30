#pragma once

#include <LovyanGFX.hpp>
#include "AuraEngine.h"
#include "theme_fetch.h"
#include "../face.h"
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

inline void auraOnPresentation(const char *presentation) {
  if (!presentation || !presentation[0]) return;
  g_aura.onPresentation(presentation);
#if USE_AURA_THEME_SYNC
  const String body = fetchThemeFile(presentation, "colors.json");
  if (body.length() > 0) {
    g_aura.loadThemeColorsJson(body.c_str(), body.length());
  }
#endif
  if (strcasecmp(presentation, "kitt") == 0) {
    g_aura.setSceneByName("dashboard");
  } else {
    g_aura.setSceneByName("idle");
  }
}

inline void auraOnBenderDraw(class Face *face) {
  if (!face) return;
  g_aura.setSceneByName(face->isTalking() ? "conversation" : "idle");
}

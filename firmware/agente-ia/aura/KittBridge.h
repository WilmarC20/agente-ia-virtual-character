#pragma once

#include <LovyanGFX.hpp>
#include "AuraEngine.h"
#include "ThemeStore.h"
#include "IconAtlas.h"
#include "theme_fetch.h"
#include "../config.h"
#include "../face_kitt.h"
#include "KittMusicLayout.h"

inline void auraDrawKitt(lgfx::LGFX_Sprite &canvas, const KittDrawCtx &ctx) {
  g_aura.drawKitt(canvas, ctx);
}

inline bool auraHitSettingsP4(int sx, int sy) {
  return g_aura.hitSettingsP4(sx, sy);
}

inline bool auraKittMusicUi() {
  return g_aura.scene() == AuraScene::Music || g_aura.musicPlayer().playing();
}

inline KittButton auraHitKittButton(int sx, int sy) {
  if (auraKittMusicUi()) return KittMusicLayout::hit(sx, sy);
  return g_aura.hitKittButton(sx, sy);
}

inline void auraSetKittActiveButton(KittButton btn, uint32_t ms = 550) {
  g_aura.setActiveKittButton(btn, ms);
}

inline void auraBindFace(class Face *face) {
  g_aura.bindFace(face);
}

inline void auraApplyEmotion(const char *emotion, float intensity, uint32_t recoveryMs = 8000) {
  g_aura.applyEmotionState(emotion, intensity, recoveryMs);
}

inline void auraSetScene(const char *scene, const char *title = nullptr) {
  if (!scene) return;
  g_aura.setSceneByName(scene);
  if (title && strcasecmp(scene, "music") == 0) {
    g_aura.musicPlayer().setPlaying(true, title);
  }
}

inline void auraOnPresentation(const char *presentation) {
  if (!presentation || !presentation[0]) return;
  g_aura.onPresentation(presentation);
#if USE_AURA_THEME_SYNC
  const String body = fetchThemeFile(presentation, "colors.json");
  if (body.length() > 0) {
    g_themeStore.saveFile(presentation, "colors.json", (const uint8_t *)body.c_str(), body.length());
    g_aura.loadThemeColorsJson(body.c_str(), body.length());
  } else {
    g_aura.loadThemeFromStore(presentation);
  }
#endif
  if (strcasecmp(presentation, "kitt") == 0) {
    auraLoadKittAtlas();
    g_aura.setSceneByName("dashboard");
  } else {
    g_aura.setSceneByName("idle");
  }
}

inline void auraOnBenderDraw(class Face *face) {
  if (!face) return;
  g_aura.setSceneByName(face->isTalking() ? "conversation" : "idle");
}

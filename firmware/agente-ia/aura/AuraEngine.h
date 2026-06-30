#pragma once

#include <LovyanGFX.hpp>
#include "ThemeManager.h"
#include "LayoutManager.h"
#include "AnimationEngine.h"
#include "SceneManager.h"
#include "SceneRenderer.h"
#include "ExpressionEngine.h"
#include "ThemeStore.h"
#include "widgets/KittDashboard.h"
#include "widgets/Popup.h"
#include "widgets/Toast.h"
#include "widgets/MusicPlayer.h"
#include "widgets/StatusCard.h"
#include "../face_kitt.h"

class AuraEngine {
 public:
  AuraEngine() {
    _theme.loadKittDefaults();
    _layout.loadKittDefaults();
    _scene.setScene(AuraScene::Dashboard);
  }

  void bindFace(class Face *face) { _expression.bind(face); }

  void setSceneByName(const char *name, uint32_t transitionMs = 0) {
    const AuraScene prev = _scene.scene();
    _scene.setSceneByName(name, transitionMs);
    if (prev != _scene.scene()) _renderer.onSceneChange(_scene.scene(), _music);
  }

  AuraScene scene() const { return _scene.scene(); }

  void applyEmotionState(const char *emotion, float intensity, uint32_t recoveryMs = 8000) {
    EmotionStatePacket pkt;
    pkt.emotion = emotion ? emotion : "neutral";
    pkt.intensity = intensity;
    pkt.recoveryMs = recoveryMs;
    _expression.apply(pkt);
  }

  void onPresentation(const char *presentation) {
    if (presentation && strcasecmp(presentation, "kitt") == 0) {
      _theme.loadKittDefaults();
      _layout.loadKittDefaults();
      _scene.setScene(AuraScene::Dashboard);
    } else if (presentation && strcasecmp(presentation, "bender") == 0) {
      _layout.loadBenderDefaults();
      _scene.setScene(AuraScene::Idle);
    } else {
      _scene.setScene(AuraScene::Idle);
    }
  }

  bool loadThemeColorsJson(const char *json, size_t len) {
    return _theme.loadColorsFromJson(json, len);
  }

  bool loadThemeFromStore(const char *themeId) {
    return g_themeStore.loadIntoTheme(themeId, _theme);
  }

  void showToast(const char *msg) { _toast.push(msg); }
  void showPopup(const char *msg, uint32_t ms = 3000) { _popup.show(msg, ms); }

  void drawKitt(lgfx::LGFX_Sprite &canvas, const KittDrawCtx &ctx) {
    _renderer.drawKitt(canvas, ctx, _scene.scene(), _theme, _layout, _anim, _dashboard, _music, _status);
    _toast.draw(canvas, _layout.layout().canvasW, _theme.theme());
    _popup.draw(canvas, _layout.layout().canvasW, _layout.layout().canvasH, _theme.theme());
  }

  bool hitSettingsP4(int sx, int sy) const {
    AuraRect z = _layout.hitZoneSettingsP4();
    return sx >= z.x && sx <= z.x + z.w && sy >= z.y && sy <= z.y + z.h;
  }

  int canvasW() const { return _layout.layout().canvasW; }
  int canvasH() const { return _layout.layout().canvasH; }

  MusicPlayerWidget &musicPlayer() { return _music; }
  ThemeManager &themeManager() { return _theme; }

 private:
  ThemeManager _theme;
  LayoutManager _layout;
  AnimationEngine _anim;
  SceneManager _scene;
  SceneRenderer _renderer;
  ExpressionEngine _expression;
  KittDashboard _dashboard;
  PopupWidget _popup;
  ToastWidget _toast;
  MusicPlayerWidget _music;
  StatusCardWidget _status;
};

#include "ExpressionEngine_impl.h"

inline AuraEngine g_aura;

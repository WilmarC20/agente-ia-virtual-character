#pragma once

#include <LovyanGFX.hpp>
#include "ThemeManager.h"
#include "LayoutManager.h"
#include "AnimationEngine.h"
#include "SceneManager.h"
#include "ExpressionEngine.h"
#include "widgets/KittDashboard.h"
#include "widgets/Popup.h"
#include "widgets/Toast.h"
#include "widgets/MusicPlayer.h"
#include "../face_kitt.h"

class AuraEngine {
 public:
  AuraEngine() {
    _theme.loadKittDefaults();
    _layout.loadKittDefaults();
    _scene.setScene(AuraScene::Dashboard);
  }

  void bindFace(class Face *face) { _expression.bind(face); }

  void setSceneByName(const char *name) { _scene.setSceneByName(name); }
  AuraScene scene() const { return _scene.scene(); }

  void applyEmotionState(const char *emotion, float intensity, uint32_t recoveryMs = 8000) {
    EmotionStatePacket pkt;
    pkt.emotion = emotion ? emotion : "neutral";
    pkt.intensity = intensity;
    pkt.recoveryMs = recoveryMs;
    _expression.apply(pkt);
  }

  void showToast(const char *msg) { _toast.push(msg); }
  void showPopup(const char *msg, uint32_t ms = 3000) { _popup.show(msg, ms); }

  void drawKitt(lgfx::LGFX_Sprite &canvas, const KittDrawCtx &ctx) {
    _dashboard.draw(canvas, ctx, _theme, _layout, _anim);
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

 private:
  ThemeManager _theme;
  LayoutManager _layout;
  AnimationEngine _anim;
  SceneManager _scene;
  ExpressionEngine _expression;
  KittDashboard _dashboard;
  PopupWidget _popup;
  ToastWidget _toast;
  MusicPlayerWidget _music;
};

#include "ExpressionEngine_impl.h"

inline AuraEngine g_aura;

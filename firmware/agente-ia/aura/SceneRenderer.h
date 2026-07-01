#pragma once

#include "SceneManager.h"
#include "widgets/KittDashboard.h"
#include "widgets/MusicPlayer.h"
#include "widgets/StatusCard.h"
#include "../face_kitt.h"

// Orquesta escenas AURA → widgets activos.
class SceneRenderer {
 public:
  void drawKitt(lgfx::LGFX_Sprite &canvas, const KittDrawCtx &ctx, AuraScene scene,
                ThemeManager &theme, LayoutManager &layout, AnimationEngine &anim,
                KittDashboard &dashboard, MusicPlayerWidget &music, StatusCardWidget &status,
                KittButton activeButton = KittButton::None, const char *musicTitle = nullptr,
                const char *musicStatus = nullptr) {
    const bool musicUi = (scene == AuraScene::Music) || music.playing();
    dashboard.draw(canvas, ctx, theme, layout, anim, activeButton, musicUi, musicTitle, musicStatus);
    (void)status;
  }

  void onSceneChange(AuraScene s, MusicPlayerWidget &music) {
    if (s != AuraScene::Music) music.setPlaying(false);
  }
};

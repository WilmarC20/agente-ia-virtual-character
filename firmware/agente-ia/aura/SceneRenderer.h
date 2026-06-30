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
                KittDashboard &dashboard, MusicPlayerWidget &music, StatusCardWidget &status) {
    dashboard.draw(canvas, ctx, theme, layout, anim);
    if (scene == AuraScene::Music) {
      AuraRect title = layout.get("titleStrip");
      if (title.valid) {
        status.draw(canvas, title.x, title.y, title.w, title.h, theme.theme(), "MUSIC",
                    music.title().c_str());
      }
    }
  }

  void onSceneChange(AuraScene s, MusicPlayerWidget &music) {
    if (s != AuraScene::Music) music.setPlaying(false);
  }
};

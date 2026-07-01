#pragma once

#include "../Renderer.h"
#include "../AtlasText.h"
#include "../AnimationEngine.h"
#include "../IconAtlas.h"
#include "../MusicIcons.h"
#include "../KittMusicLayout.h"
#include "../../config.h"
#include "../../face_kitt.h"

// Escena KITT — atlas sprite (referencia) + modulador animado encima.
class KittDashboard {
 public:
  void draw(lgfx::LGFX_Sprite &canvas, const KittDrawCtx &ctx,
            ThemeManager &themeMgr, LayoutManager &layoutMgr, AnimationEngine &anim,
            KittButton activeButton = KittButton::None, bool musicMode = false,
            const char *musicTitle = nullptr, const char *musicStatus = nullptr) {
    if (musicMode) {
      drawKittMusicScreen(canvas, ctx, themeMgr, layoutMgr, anim, activeButton, musicTitle, musicStatus);
      return;
    }

#if USE_KITT_SPRITE_ATLAS
    if (g_kittAtlas.begin()) {
      g_kittAtlas.drawDashboard(canvas);
      AuraRenderer r;
      r.bind(&canvas);
      g_kittAtlas.clearModulatorZone(canvas, layoutMgr);
      anim.drawModulator(r, layoutMgr, themeMgr.theme(), ctx);
      drawActiveButton(canvas, layoutMgr, themeMgr.theme(), activeButton);
      return;
    }
    static bool logged = false;
    if (!logged) {
      logged = true;
      Serial.println("KITT atlas: fallback procedural");
    }
#endif

    AuraRenderer r;
    r.bind(&canvas);
    const AuraTheme &T = themeMgr.theme();
    const AuraLayout &L = layoutMgr.layout();

    r.fillBackground(T.background);

    struct Bar { uint16_t col; const char *label; };
    static const Bar tops[] = {
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
    AtlasText::drawTwoLine(canvas, autoBar.x, autoBar.y, autoBar.w, autoBar.h, T.labelFg, T.orange, "AUTO",
                           "CRUISE");
    r.fillRoundRect(normalBar.x, normalBar.y, normalBar.w, normalBar.h, L.botRadius, T.yellow);
    AtlasText::drawTwoLine(canvas, normalBar.x, normalBar.y, normalBar.w, normalBar.h, T.labelFg, T.yellow, "NORMAL",
                           "CRUISE");
    r.fillRoundRect(pursuitBar.x, pursuitBar.y, pursuitBar.w, pursuitBar.h, L.botRadius, T.blue);
    AtlasText::drawCentered(canvas, pursuitBar.x, pursuitBar.y, pursuitBar.w, pursuitBar.h, T.labelFg, T.blue,
                            "PURSUIT");
    drawActiveButton(canvas, layoutMgr, T, activeButton);
  }

 private:
  static void drawKittMusicScreen(lgfx::LGFX_Sprite &canvas, const KittDrawCtx &ctx,
                                  ThemeManager &themeMgr, LayoutManager &layoutMgr, AnimationEngine &anim,
                                  KittButton active, const char *musicTitle, const char *musicStatus) {
    const AuraTheme &T = themeMgr.theme();
    canvas.fillSprite(T.background);

    // Líneas KITT
    canvas.drawFastHLine(0, KittMusicLayout::kSepTopY, KittMusicLayout::kW, T.red);
    canvas.drawFastHLine(0, KittMusicLayout::kSepBotY, KittMusicLayout::kW, T.red);

    // Título único (sin overlay externo)
    AuraRect titleR = KittMusicLayout::titleBar();
    canvas.fillRoundRect(titleR.x, titleR.y, titleR.w, titleR.h, 5, 0x1082);
    canvas.drawRoundRect(titleR.x, titleR.y, titleR.w, titleR.h, 5, T.orange);
    MusicIcons::drawNote(canvas, titleR.x + 10, titleR.y + titleR.h / 2, 12, T.orange);

    String line;
    if (musicStatus && musicStatus[0]) line = musicStatus;
    else if (musicTitle && musicTitle[0]) line = musicTitle;
    canvas.setFont(&fonts::DejaVu12);
    canvas.setTextColor(TFT_WHITE, 0x1082);
    canvas.setTextWrap(false);
    const int maxPx = titleR.w - 28;
    while (line.length() > 0 && canvas.textWidth(line) > maxPx) {
      if (line.length() <= 4) break;
      line = line.substring(0, line.length() - 4) + "...";
    }
    canvas.setCursor(titleR.x + 22, titleR.y + (titleR.h - canvas.fontHeight()) / 2);
    canvas.print(line);

    // Espectro central (zona amplia, sin óvalos laterales)
    AuraRect mz = KittMusicLayout::modulatorZone();
    canvas.fillRect(mz.x, mz.y, mz.w, mz.h, T.background);
    AuraRenderer r;
    r.bind(&canvas);
    anim.drawModulator(r, layoutMgr, T, ctx);

    // Barra de transporte: 6 iconos iguales
    struct Slot {
      MusicIcons::Kind icon;
      uint16_t col;
      KittButton id;
    };
    const Slot slots[] = {
        {MusicIcons::Kind::Prev, T.orange, KittButton::AutoCruise},
        {MusicIcons::Kind::Stop, T.red, KittButton::NormalCruise},
        {MusicIcons::Kind::Next, T.blue, KittButton::Pursuit},
        {MusicIcons::Kind::Minus, T.yellow, KittButton::MinRpm},
        {MusicIcons::Kind::Plus, T.yellow, KittButton::FuelOn},
        {MusicIcons::Kind::Loop, T.orange, KittButton::Ignitors},
    };
    for (int i = 0; i < KittMusicLayout::kBtnCount; i++) {
      AuraRect btn = KittMusicLayout::transportButton(i);
      MusicIcons::drawButton(canvas, btn.x, btn.y, btn.w, btn.h, 6, slots[i].col, slots[i].icon, T.labelFg,
                             active == slots[i].id);
    }
  }

  static void drawActiveButton(lgfx::LGFX_Sprite &canvas, LayoutManager &layoutMgr,
                               const AuraTheme &theme, KittButton btn) {
    if (btn == KittButton::None || btn == KittButton::Modulator) return;

    AuraRect r{};
    const char *label = nullptr;
    const char *l1 = nullptr;
    const char *l2 = nullptr;
    uint16_t bg = 0x07FF;
    uint16_t fg = 0x0000;
    int radius = 6;

    switch (btn) {
      case KittButton::Power: r = layoutMgr.topBar(0); label = "POWER"; radius = 4; break;
      case KittButton::MinRpm: r = layoutMgr.topBar(1); label = "MIN RPM"; radius = 4; break;
      case KittButton::FuelOn: r = layoutMgr.topBar(2); label = "FUEL ON"; radius = 4; break;
      case KittButton::Ignitors: r = layoutMgr.topBar(3); label = "IGNITORS"; radius = 4; break;
      case KittButton::Air: r = layoutMgr.ovalLeft(0); label = "AIR"; radius = r.h / 2; break;
      case KittButton::Oil: r = layoutMgr.ovalLeft(1); label = "OIL"; radius = r.h / 2; break;
      case KittButton::P1: r = layoutMgr.ovalLeft(2); label = "P1"; radius = r.h / 2; break;
      case KittButton::P2: r = layoutMgr.ovalLeft(3); label = "P2"; radius = r.h / 2; break;
      case KittButton::S1: r = layoutMgr.ovalRight(0); label = "S1"; radius = r.h / 2; break;
      case KittButton::S2: r = layoutMgr.ovalRight(1); label = "S2"; radius = r.h / 2; break;
      case KittButton::P3: r = layoutMgr.ovalRight(2); label = "P3"; radius = r.h / 2; break;
      case KittButton::P4: r = layoutMgr.ovalRight(3); label = "P4"; radius = r.h / 2; break;
      case KittButton::AutoCruise:
        r = layoutMgr.bottomBar(0);
        l1 = "AUTO";
        l2 = "CRUISE";
        radius = 8;
        break;
      case KittButton::NormalCruise:
        r = layoutMgr.bottomBar(1);
        l1 = "NORMAL";
        l2 = "CRUISE";
        radius = 8;
        break;
      case KittButton::Pursuit:
        r = layoutMgr.bottomBar(2);
        label = "PURSUIT";
        bg = theme.blue;
        fg = 0xFFFF;
        radius = 8;
        break;
      default: return;
    }
    if (!r.valid) return;
    canvas.fillRoundRect(r.x, r.y, r.w, r.h, radius, bg);
    canvas.drawRoundRect(r.x, r.y, r.w, r.h, radius, 0xFFFF);
    if (l1 && l2) {
      AtlasText::drawTwoLine(canvas, r.x, r.y, r.w, r.h, fg, bg, l1, l2);
    } else if (label) {
      AtlasText::drawCentered(canvas, r.x, r.y, r.w, r.h, fg, bg, label);
    }
  }
};

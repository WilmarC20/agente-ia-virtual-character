// Controles de música en pantalla (portrait 240x320 o landscape 320x240).
#pragma once

#include <LovyanGFX.hpp>
#include "touch.h"
#include "es8311.h"
#include "settings.h"
#include "aura/MusicIcons.h"

extern volatile bool g_musicStopRequested;
void musicRequestNext();
void musicRequestPrev();

namespace MusicScreen {

inline int titleBandH() { return 28; }
// El sprite de la cara ocupa y=12..212 (FACE_OFFSET_Y + FACE_H). Los controles
// deben quedar DEBAJO para no ser tapados por el blit del doble búfer.
inline int barY(int screenH) { return screenH > 212 ? 212 : screenH - 28; }

inline void drawTitle(lgfx::LGFX_Device &gfx, const String &title) {
  const int W = gfx.width();
  const int H = titleBandH();
  gfx.fillRect(0, 0, W, H, 0x0841);
  gfx.drawFastHLine(0, H - 1, W, 0x4208);
  gfx.setTextColor(TFT_CYAN, 0x0841);
  gfx.setFont(&fonts::DejaVu12);
  gfx.setTextWrap(false);
  String line = title;
  const int maxLen = (W <= 250) ? 26 : 34;
  if (line.length() > (unsigned)maxLen) line = line.substring(0, maxLen - 3) + "...";
  gfx.setCursor(6, 4);
  gfx.print(line);
}

inline void drawControls(lgfx::LGFX_Device &gfx, uint8_t volume) {
  const int W = gfx.width();
  const int H = gfx.height();
  const int by = barY(H);
  const int bh = H - by;
  gfx.fillRect(0, by, W, bh, 0x0841);
  gfx.drawFastHLine(0, by, W, 0x4208);

  const int btnH = bh - 6;
  const int y = by + 3;
  if (W <= 250) {
    MusicIcons::drawButton(gfx, 4, y, 38, btnH, 4, 0x3186, MusicIcons::Kind::Prev, TFT_WHITE);
    MusicIcons::drawButton(gfx, 46, y, 42, btnH, 4, TFT_MAROON, MusicIcons::Kind::Stop, TFT_WHITE);
    MusicIcons::drawButton(gfx, 92, y, 38, btnH, 4, 0x3186, MusicIcons::Kind::Next, TFT_WHITE);
    MusicIcons::drawButton(gfx, W - 64, y, 28, btnH, 4, 0x3186, MusicIcons::Kind::Minus, TFT_WHITE);
    MusicIcons::drawButton(gfx, W - 32, y, 28, btnH, 4, 0x3186, MusicIcons::Kind::Plus, TFT_WHITE);
  } else {
    MusicIcons::drawButton(gfx, 8, y, 46, btnH, 4, 0x3186, MusicIcons::Kind::Prev, TFT_WHITE);
    MusicIcons::drawButton(gfx, 60, y, 54, btnH, 4, TFT_MAROON, MusicIcons::Kind::Stop, TFT_WHITE);
    MusicIcons::drawButton(gfx, 120, y, 46, btnH, 4, 0x3186, MusicIcons::Kind::Next, TFT_WHITE);
    MusicIcons::drawButton(gfx, 178, y, 32, btnH, 4, 0x3186, MusicIcons::Kind::Minus, TFT_WHITE);
    MusicIcons::drawButton(gfx, 216, y, 32, btnH, 4, 0x3186, MusicIcons::Kind::Plus, TFT_WHITE);
    gfx.setTextColor(TFT_WHITE, 0x0841);
    gfx.setFont(&fonts::DejaVu12);
    gfx.setCursor(272, by + 8);
    gfx.printf("%u%%", volume);
  }
}

inline void drawNowPlaying(lgfx::LGFX_Device &gfx, const String &title, uint8_t volume) {
  (void)title;
  drawControls(gfx, volume);
}

inline void pollTouch(lgfx::LGFX_Device &gfx, AppSettings &settings, ES8311 &codec) {
  static uint32_t lastAct = 0;
  int sx, sy;
  if (!touchReadPoint(gfx.width(), gfx.height(), sx, sy)) return;
  if (millis() - lastAct < 260) return;
  const int by = barY(gfx.height());
  if (sy < by) return;
  lastAct = millis();

  const int W = gfx.width();

  auto volDown = [&]() {
    if (settings.volume >= 5) settings.volume -= 5;
    else settings.volume = 0;
    codec.setPlaybackVolumePercent(settings.volume);
    saveSettings(settings);
    drawControls(gfx, settings.volume);
  };
  auto volUp = [&]() {
    if (settings.volume <= 95) settings.volume += 5;
    else settings.volume = 100;
    codec.setPlaybackVolumePercent(settings.volume);
    saveSettings(settings);
    drawControls(gfx, settings.volume);
  };

  if (W <= 250) {
    if (sx >= 4 && sx <= 42) {
      musicRequestPrev();
      return;
    }
    if (sx >= 46 && sx <= 88) {
      g_musicStopRequested = true;
      return;
    }
    if (sx >= 92 && sx <= 130) {
      musicRequestNext();
      return;
    }
    if (sx >= W - 32) {
      volUp();
      return;
    }
    if (sx >= W - 64 && sx <= W - 34) {
      volDown();
    }
  } else {
    if (sx >= 8 && sx <= 54) {
      musicRequestPrev();
      return;
    }
    if (sx >= 60 && sx <= 114) {
      g_musicStopRequested = true;
      return;
    }
    if (sx >= 120 && sx <= 166) {
      musicRequestNext();
      return;
    }
    if (sx >= 178 && sx <= 210) {
      volDown();
      return;
    }
    if (sx >= 216 && sx <= 248) {
      volUp();
    }
  }
}

}  // namespace MusicScreen

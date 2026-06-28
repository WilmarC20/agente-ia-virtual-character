// Controles de música en pantalla (portrait 240x320 o landscape 320x240).
#pragma once

#include <LovyanGFX.hpp>
#include "touch.h"
#include "es8311.h"
#include "settings.h"

extern volatile bool g_musicStopRequested;

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

  auto btn = [&](int x, int w, const char *label, uint16_t fg, uint16_t bg) {
    gfx.fillRoundRect(x, by + 3, w, bh - 6, 4, bg);
    gfx.setTextColor(fg, bg);
    gfx.setFont(&fonts::DejaVu12);
    gfx.setCursor(x + 4, by + 8);
    gfx.print(label);
  };

  if (W <= 250) {
    btn(4, 34, "-", TFT_WHITE, 0x3186);
    btn(42, 34, "+", TFT_WHITE, 0x3186);
    gfx.setTextColor(TFT_WHITE, 0x0841);
    gfx.setFont(&fonts::DejaVu12);
    gfx.setCursor(84, by + 8);
    gfx.printf("Vol %u%%", volume);
    btn(W - 54, 50, "STOP", TFT_WHITE, TFT_MAROON);
  } else {
    btn(6, 36, "-", TFT_WHITE, 0x3186);
    btn(52, 36, "+", TFT_WHITE, 0x3186);
    gfx.setTextColor(TFT_WHITE, 0x0841);
    gfx.setFont(&fonts::DejaVu12);
    gfx.setCursor(102, by + 8);
    gfx.printf("Vol %u%%", volume);
    btn(222, 92, "STOP", TFT_WHITE, TFT_MAROON);
  }
}

// El título ya NO se dibuja aquí: va dentro del sprite de la cara (face.setTopTitle),
// que es doble búfer => transparente y sin parpadeo. Aquí solo los controles.
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
  if (W <= 250) {
    if (sx >= W - 54) {
      g_musicStopRequested = true;
      return;
    }
    if (sx >= 4 && sx <= 38) {
      if (settings.volume >= 5) settings.volume -= 5;
      else settings.volume = 0;
      codec.setPlaybackVolumePercent(settings.volume);
      saveSettings(settings);
      drawControls(gfx, settings.volume);
      return;
    }
    if (sx >= 42 && sx <= 76) {
      if (settings.volume <= 95) settings.volume += 5;
      else settings.volume = 100;
      codec.setPlaybackVolumePercent(settings.volume);
      saveSettings(settings);
      drawControls(gfx, settings.volume);
    }
  } else {
    if (sx >= 222) {
      g_musicStopRequested = true;
      return;
    }
    if (sx >= 6 && sx <= 42) {
      if (settings.volume >= 5) settings.volume -= 5;
      else settings.volume = 0;
      codec.setPlaybackVolumePercent(settings.volume);
      saveSettings(settings);
      drawControls(gfx, settings.volume);
      return;
    }
    if (sx >= 52 && sx <= 88) {
      if (settings.volume <= 95) settings.volume += 5;
      else settings.volume = 100;
      codec.setPlaybackVolumePercent(settings.volume);
      saveSettings(settings);
      drawControls(gfx, settings.volume);
    }
  }
}

}  // namespace MusicScreen

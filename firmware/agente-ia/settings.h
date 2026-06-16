// On-screen settings: a touch-driven menu (opened from the gear icon) plus the
// persisted user preferences behind it. Stored in NVS (Preferences) so they survive
// reboots. Renders directly to the display (the face sprite is paused while open).
#pragma once

#include <Preferences.h>
#include <LovyanGFX.hpp>
#include "config.h"
#include "touch.h"
#include "es8311.h"

// Wake phrases the user can pick on the device. Labels MUST match the server keys in
// main.py WAKE_PRESETS — the device sends the label, the server matches against it.
// A 2.8" screen has no keyboard, so this is a preset picker, not free text.
static const char *const WAKE_PRESET_LABELS[] = {
    "hola asistente", "che robot", "ey bender", "hola bender",
};
static constexpr int WAKE_PRESET_COUNT = sizeof(WAKE_PRESET_LABELS) / sizeof(WAKE_PRESET_LABELS[0]);

struct AppSettings {
  uint8_t volume = TTS_VOLUME_PERCENT;  // 0..100, stepped by 10
  bool voiceWake = true;                // PC-side "Hola asistente" listener on/off
  uint8_t phraseIdx = 0;                // index into WAKE_PRESET_LABELS
};

inline void loadSettings(AppSettings &s) {
  Preferences p;
  p.begin("agente", true);              // read-only
  s.volume = p.getUChar("vol", s.volume);
  s.voiceWake = p.getBool("voice", s.voiceWake);
  s.phraseIdx = p.getUChar("phrase", s.phraseIdx);
  p.end();
  if (s.volume > 100) s.volume = 100;
  if (s.phraseIdx >= WAKE_PRESET_COUNT) s.phraseIdx = 0;
}

inline void saveSettings(const AppSettings &s) {
  Preferences p;
  p.begin("agente", false);
  p.putUChar("vol", s.volume);
  p.putBool("voice", s.voiceWake);
  p.putUChar("phrase", s.phraseIdx);
  p.end();
}

class SettingsScreen {
public:
  // Runs the modal menu: blocks, handling touch, until the user taps "Guardar".
  // Mutates s and applies volume live; persists on exit. Returns when closed.
  void run(lgfx::LGFX_Device &gfx, AppSettings &s, ES8311 &codec) {
    const int W = gfx.width(), H = gfx.height();
    bool dirty = true;
    uint32_t lastAct = 0;

    while (true) {
      if (dirty) { draw(gfx, s); dirty = false; }

      int sx, sy;
      if (touchReadPoint(W, H, sx, sy) && millis() - lastAct > 220) {
        lastAct = millis();
        if (hit(volMinus, sx, sy)) {
          if (s.volume >= 10) s.volume -= 10;
          codec.setPlaybackVolumePercent(s.volume);
          dirty = true;
        } else if (hit(volPlus, sx, sy)) {
          if (s.volume <= 90) s.volume += 10;
          codec.setPlaybackVolumePercent(s.volume);
          dirty = true;
        } else if (hit(voiceToggle, sx, sy)) {
          s.voiceWake = !s.voiceWake;
          dirty = true;
        } else if (hit(phraseBtn, sx, sy)) {
          s.phraseIdx = (s.phraseIdx + 1) % WAKE_PRESET_COUNT;
          dirty = true;
        } else if (hit(saveBtn, sx, sy)) {
          break;
        }
      }
      delay(15);
    }

    saveSettings(s);
    touchWaitRelease(W, H);   // don't let the closing tap leak into the main loop
  }

private:
  struct Rect { int x, y, w, h; };
  // Layout for a 320x240 landscape screen.
  Rect volMinus{150, 44, 40, 36};
  Rect volPlus{250, 44, 40, 36};
  Rect voiceToggle{168, 96, 138, 36};
  Rect phraseBtn{198, 150, 108, 36};
  Rect saveBtn{50, 198, 220, 36};

  static constexpr uint16_t BG = 0x18E3;     // dark slate
  static constexpr uint16_t LINE = 0x4208;   // separators
  static constexpr uint16_t BTN = 0x3A6E;    // neutral button
  static constexpr uint16_t GREEN = 0x05E0;  // on / save
  static constexpr uint16_t RED = 0xC080;    // off

  static bool hit(const Rect &r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
  }

  // Centre text manually (textWidth/fontHeight) to avoid relying on text-datum enums.
  void button(lgfx::LGFX_Device &gfx, const Rect &r, const char *txt, uint16_t col) {
    gfx.fillRoundRect(r.x, r.y, r.w, r.h, 7, col);
    gfx.setFont(&fonts::DejaVu18);
    gfx.setTextColor(TFT_WHITE, col);
    int tw = gfx.textWidth(txt), th = gfx.fontHeight();
    gfx.setCursor(r.x + (r.w - tw) / 2, r.y + (r.h - th) / 2);
    gfx.print(txt);
  }

  void label(lgfx::LGFX_Device &gfx, int x, int y, const char *t) {
    gfx.setFont(&fonts::DejaVu18);
    gfx.setTextColor(TFT_WHITE, BG);
    gfx.setCursor(x, y);
    gfx.print(t);
  }

  void draw(lgfx::LGFX_Device &gfx, const AppSettings &s) {
    const int W = gfx.width();
    gfx.fillScreen(BG);

    gfx.setFont(&fonts::DejaVu18);
    gfx.setTextColor(TFT_WHITE, BG);
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.setCursor(12, 8);
    gfx.print("Configuracion");
    gfx.drawFastHLine(0, 36, W, LINE);

    // Volume
    label(gfx, 12, 52, "Volumen");
    button(gfx, volMinus, "-", BTN);
    button(gfx, volPlus, "+", BTN);
    gfx.setFont(&fonts::DejaVu18);
    gfx.setTextColor(TFT_CYAN, BG);
    char vbuf[8];
    snprintf(vbuf, sizeof(vbuf), "%d%%", s.volume);
    int cxv = (volMinus.x + volMinus.w + volPlus.x) / 2;
    gfx.setCursor(cxv - gfx.textWidth(vbuf) / 2, volMinus.y + 8);
    gfx.print(vbuf);

    // Voice wake
    label(gfx, 12, 104, "Voz (gatillo)");
    button(gfx, voiceToggle, s.voiceWake ? "ACTIVADA" : "APAGADA", s.voiceWake ? GREEN : RED);

    // Wake phrase
    label(gfx, 12, 150, "Palabra:");
    button(gfx, phraseBtn, "Cambiar", BTN);
    gfx.setFont(&fonts::DejaVu12);
    gfx.setTextColor(TFT_YELLOW, BG);
    gfx.setCursor(12, 174);
    gfx.print(WAKE_PRESET_LABELS[s.phraseIdx]);

    // Save & exit
    button(gfx, saveBtn, "Guardar y salir", GREEN);
  }
};

// On-screen settings: touch menu (gear icon) + NVS persistence.
#pragma once

#include <Preferences.h>
#include <LovyanGFX.hpp>
#include "config.h"
#include "touch.h"
#include "es8311.h"

static const char *const WAKE_PRESET_LABELS[] = {
    "hola asistente", "che robot", "ey bender", "hola bender",
};
static constexpr int WAKE_PRESET_COUNT = sizeof(WAKE_PRESET_LABELS) / sizeof(WAKE_PRESET_LABELS[0]);

struct AppSettings {
  uint8_t volume = TTS_VOLUME_PERCENT;
  bool voiceWake = true;
#if ENABLE_WAKEWORD
  bool hiEspWake = false;
#endif
  uint8_t phraseIdx = 0;
  bool idleRemarks = ENABLE_IDLE_REMARKS;
  uint8_t mouthAnim = 0;         // talking mouth style: 0 bars, 1 waves, 2 lips
  uint8_t speechCaption = 0;     // 0 off, 1 karaoke, 2 ascii plain
  uint8_t vibingMic = 150;       // gain % (50–300)
  uint16_t vibingFloor = 100;    // piso ruido 0–500 (raw tras gain)
  uint16_t vibingCeil = 500;     // techo 200–900 (raw tras gain = barras al máximo)
  uint8_t storyPlayMode = 0;     // story audio test: 0=current,1=tts-like,2=music+amp,3=wav-mode
};

inline void loadSettings(AppSettings &s) {
  Preferences p;
  p.begin("agente", true);
  s.volume = p.getUChar("vol", s.volume);
  s.voiceWake = p.getBool("voice", s.voiceWake);
#if ENABLE_WAKEWORD
  // Hi ESP (ESP-SR) defaults OFF: running WakeNet shares the I2S bus and leaves the
  // mic at half rate after TTS (2nd recording came out accelerated). Touch + PC-side
  // "Hola asistente" still wake the device. Re-enable manually only if you accept that.
  s.hiEspWake = p.getBool("hiesp", false);
#endif
  s.phraseIdx = p.getUChar("phrase", s.phraseIdx);
  s.idleRemarks = p.getBool("idle", s.idleRemarks);
  s.mouthAnim = p.getUChar("manim", s.mouthAnim);
  if (s.mouthAnim > 2) s.mouthAnim = 0;
  s.speechCaption = p.getUChar("scap", s.speechCaption);
  if (s.speechCaption > 2) s.speechCaption = 0;
  s.vibingMic = p.getUChar("vmic", s.vibingMic);
  s.storyPlayMode = p.getUChar("spm", 0);
  if (s.storyPlayMode > 3) s.storyPlayMode = 0;
  {
    uint16_t flo = p.getUShort("vflo", 0);
    uint16_t cei = p.getUShort("vcei", 0);
    if (flo == 0 && cei == 0) {
      uint8_t flo8 = p.getUChar("vflo", 0);
      uint8_t cei8 = p.getUChar("vcei", 0);
      if (flo8 > 0 || cei8 > 0) {
        flo = (flo8 > 0) ? (uint16_t)flo8 * 5u : 100u;
        cei = (cei8 > 0) ? (uint16_t)cei8 * 5u : 500u;
      }
    }
    if (flo > 0) s.vibingFloor = flo;
    if (cei > 0) s.vibingCeil = cei;
  }
  p.end();
  if (s.volume > 100) s.volume = 100;
  if (s.phraseIdx >= WAKE_PRESET_COUNT) s.phraseIdx = 0;
  if (s.vibingMic < 50) s.vibingMic = 50;
  if (s.vibingMic > 300) s.vibingMic = 300;
  if (s.vibingFloor > 500) s.vibingFloor = 500;
  if (s.vibingCeil < 200) s.vibingCeil = 200;
  if (s.vibingCeil > 900) s.vibingCeil = 900;
  if (s.vibingFloor >= s.vibingCeil - 40) {
    s.vibingFloor = (s.vibingCeil > 40) ? (uint16_t)(s.vibingCeil - 40) : 0;
  }
}

inline void saveSettings(const AppSettings &s) {
  Preferences p;
  p.begin("agente", false);
  p.putUChar("vol", s.volume);
  p.putBool("voice", s.voiceWake);
#if ENABLE_WAKEWORD
  p.putBool("hiesp", s.hiEspWake);
#endif
  p.putUChar("phrase", s.phraseIdx);
  p.putBool("idle", s.idleRemarks);
  p.putUChar("manim", s.mouthAnim);
  p.putUChar("scap", s.speechCaption);
  p.putUChar("vmic", s.vibingMic);
  p.putUShort("vflo", s.vibingFloor);
  p.putUShort("vcei", s.vibingCeil);
  p.putUChar("spm", s.storyPlayMode);
  p.end();
}

inline void applySettingsGlobals(const AppSettings &s, uint8_t &wakePhraseIdx,
                                 bool &voiceWakeEnabled, bool &idleRemarksEnabled) {
  wakePhraseIdx = s.phraseIdx;
  voiceWakeEnabled = s.voiceWake;
  idleRemarksEnabled = s.idleRemarks;
}

class SettingsScreen {
public:
  void run(lgfx::LGFX_Device &gfx, AppSettings &s, ES8311 &codec) {
    const int W = gfx.width(), H = gfx.height();
    layoutFor(W, H);
    touchInvalidateCache();
    touchWaitRelease(W, H);
    bool dirty = true;
    uint32_t lastAct = 0;

    while (true) {
      touchBeginFrame();
      if (dirty) { draw(gfx, s); dirty = false; }

      int sx, sy;
      if (touchReadPoint(W, H, sx, sy) && millis() - lastAct > 220) {
        lastAct = millis();
        if (hit(volMinus, sx, sy)) {
          if (s.volume > 0) s.volume--;
          codec.setPlaybackVolumePercent(s.volume);
          dirty = true;
        } else if (hit(volPlus, sx, sy)) {
          if (s.volume < 100) s.volume++;
          codec.setPlaybackVolumePercent(s.volume);
          dirty = true;
        } else if (hit(voiceToggle, sx, sy)) {
          s.voiceWake = !s.voiceWake;
          dirty = true;
#if ENABLE_WAKEWORD
        } else if (hit(hiEspToggle, sx, sy)) {
          s.hiEspWake = !s.hiEspWake;
          dirty = true;
#endif
        } else if (hit(idleToggle, sx, sy)) {
          s.idleRemarks = !s.idleRemarks;
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
    touchWaitRelease(W, H);
  }

private:
  struct Rect { int x, y, w, h; };
  Rect volMinus{}, volPlus{}, voiceToggle{}, hiEspToggle{}, idleToggle{}, phraseBtn{}, saveBtn{};

  static constexpr uint16_t BG = 0x18E3;
  static constexpr uint16_t LINE = 0x4208;
  static constexpr uint16_t BTN = 0x3A6E;
  static constexpr uint16_t GREEN = 0x05E0;
  static constexpr uint16_t RED = 0xC080;

  void layoutFor(int W, int H) {
    if (W <= 250) {
      volMinus = {16, 48, 44, 32};
      volPlus = {W - 60, 48, 44, 32};
      voiceToggle = {W - 116, 86, 108, 28};
      hiEspToggle = {W - 116, 120, 108, 28};
      idleToggle = {W - 116, 154, 108, 28};
      phraseBtn = {W - 96, 188, 84, 28};
      saveBtn = {16, H - 44, W - 32, 36};
    } else {
      volMinus = {150, 38, 40, 32};
      volPlus = {250, 38, 40, 32};
      voiceToggle = {168, 78, 138, 30};
      hiEspToggle = {168, 114, 138, 30};
      idleToggle = {168, 150, 138, 30};
      phraseBtn = {198, 186, 108, 30};
      saveBtn = {50, 222, 220, 32};
    }
  }

  static bool hit(const Rect &r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
  }

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
    const bool portrait = W <= 250;
    gfx.fillScreen(BG);

    gfx.setFont(&fonts::DejaVu18);
    gfx.setTextColor(TFT_WHITE, BG);
    gfx.setCursor(12, 8);
    gfx.print("Configuracion");
    gfx.drawFastHLine(0, 36, W, LINE);

    label(gfx, 12, portrait ? 54 : 46, "Volumen");
    button(gfx, volMinus, "-", BTN);
    button(gfx, volPlus, "+", BTN);
    gfx.setFont(&fonts::DejaVu18);
    gfx.setTextColor(TFT_CYAN, BG);
    char vbuf[8];
    snprintf(vbuf, sizeof(vbuf), "%d%%", s.volume);
    int cxv = (volMinus.x + volMinus.w + volPlus.x) / 2;
    gfx.setCursor(cxv - gfx.textWidth(vbuf) / 2, volMinus.y + 6);
    gfx.print(vbuf);

    label(gfx, 12, portrait ? 92 : 84, "Gatillo voz");
    button(gfx, voiceToggle, s.voiceWake ? "ON" : "OFF", s.voiceWake ? GREEN : RED);

#if ENABLE_WAKEWORD
    label(gfx, 12, portrait ? 126 : 120, "Hi ESP");
    button(gfx, hiEspToggle, s.hiEspWake ? "ON" : "OFF", s.hiEspWake ? GREEN : RED);
#endif

    label(gfx, 12, portrait ? 160 : 156, "Idle auto");
    button(gfx, idleToggle, s.idleRemarks ? "ON" : "OFF", s.idleRemarks ? GREEN : RED);

    label(gfx, 12, portrait ? 194 : 192, "Frase:");
    button(gfx, phraseBtn, ">>", BTN);
    gfx.setFont(&fonts::DejaVu12);
    gfx.setTextColor(TFT_YELLOW, BG);
    gfx.setCursor(portrait ? 12 : 60, portrait ? 218 : 196);
    String phrase = WAKE_PRESET_LABELS[s.phraseIdx];
    if (portrait && phrase.length() > 18) phrase = phrase.substring(0, 15) + "...";
    gfx.print(phrase);

    button(gfx, saveBtn, "Guardar y salir", GREEN);
  }
};

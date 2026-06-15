// Bender face renderer (LovyanGFX) — black & white line-art, matching the reference sheet.
//
// Style: black background. Capsule visor = concentric white rims with a BLACK interior;
// the eyes are WHITE shapes with fixed black square pupils, and their SHAPE changes per
// emotion (angled tops = angry, drooped = sad, slit = sleepy, dash = wink...). The mouth
// is a white teeth grid (capsule) that bows/pinches per emotion and ripples with a sine
// wave while speaking (lip-sync). SURPRISED switches to a round visor + round eyes + "O".
//
// Double-buffered in a PSRAM sprite (no flicker). Repaints only on change (_dirty), so
// idle = no SPI traffic; talking ~25 FPS while each I2S chunk (~64ms) outlasts a push.
#pragma once

#include <Arduino.h>
#include <math.h>
#include <LovyanGFX.hpp>
#include "config.h"

enum class Emotion {
  Neutral, Happy, Sad, Angry, Surprised, Thinking, Sleepy,
  Love, Excited, Cool, Confused, Dizzy
};

inline Emotion emotionFromString(const String &s) {
  if (s == "happy") return Emotion::Happy;
  if (s == "sad") return Emotion::Sad;
  if (s == "angry") return Emotion::Angry;
  if (s == "surprised") return Emotion::Surprised;
  if (s == "thinking") return Emotion::Thinking;
  if (s == "sleepy") return Emotion::Sleepy;
  if (s == "love") return Emotion::Love;
  if (s == "excited") return Emotion::Excited;
  if (s == "cool") return Emotion::Cool;
  if (s == "confused") return Emotion::Confused;
  if (s == "dizzy") return Emotion::Dizzy;
  return Emotion::Neutral;
}

class Face {
public:
  explicit Face(lgfx::LGFX_Device &gfx) : _gfx(gfx), _canvas(&gfx) {}

  void begin() {
    _canvas.setPsram(true);
    _canvas.setColorDepth(16);
    _canvas.createSprite(_gfx.width(), FACE_H);
    _gfx.fillScreen(TFT_BLACK);
  }

  void setEmotion(Emotion e) { if (e != _emotion) { _emotion = e; _dirty = true; } }

  void setTalking(bool talking) {
    if (talking != _talking) {
      _talking = talking;
      if (!talking) _mouthAmp = 0;
      _dirty = true;
    }
  }
  bool isTalking() const { return _talking; }

  void setMouthAmplitude(uint8_t level) {
    _mouthAmp = level > 100 ? 100 : level;
    if (_talking) _dirty = true;
  }

  void update() {
    uint32_t now = millis();
    if (now >= _nextBlinkAt && !_talking) {
      _blinking = true;
      _blinkUntil = now + 110;
      _nextBlinkAt = now + 3000 + random(3000);
      _dirty = true;
    }
    if (_blinking && now >= _blinkUntil) { _blinking = false; _dirty = true; }
    if (_talking) _dirty = true;
    if (_dirty) { draw(); _dirty = false; }
  }

  void showText(const String &text, uint16_t color = TFT_WHITE) {
    int y = FACE_H + 4;
    _gfx.fillRect(0, y, _gfx.width(), _gfx.height() - y, TFT_BLACK);
    _gfx.setTextColor(color, TFT_BLACK);
    _gfx.setFont(&fonts::DejaVu18);
    _gfx.setCursor(8, y + 4);
    _gfx.setTextWrap(true);
    _gfx.print(text);
  }

  void drawMicLevel(uint32_t rms) {
    const int h = 8, w = _gfx.width(), y = _gfx.height() - h;
    int level = (int)(rms * w / 1200);
    if (level > w) level = w;
    _gfx.fillRect(0, y, level, h, (level > w * 2 / 3) ? TFT_RED : TFT_GREEN);
    _gfx.fillRect(level, y, w - level, h, 0x2104);
  }
  void clearMicLevel() { _gfx.fillRect(0, _gfx.height() - 8, _gfx.width(), 8, TFT_BLACK); }

private:
  lgfx::LGFX_Device &_gfx;
  lgfx::LGFX_Sprite _canvas;
  Emotion _emotion = Emotion::Sleepy;
  bool _dirty = true, _blinking = false, _talking = false;
  uint8_t _mouthAmp = 0;
  uint32_t _blinkUntil = 0, _nextBlinkAt = 3000;

  static constexpr int FACE_H = 200;
  static constexpr uint16_t W = 0xFFFF;   // white line-art
  static constexpr uint16_t K = 0x0000;   // black (visor interior, pupils, grid)
  static constexpr int CXC = 160;         // face centre x

  bool roundLayout() const { return _emotion == Emotion::Surprised || _emotion == Emotion::Dizzy; }

  // --- WHITE eye shape with fixed black square pupil; black carves change the shape ---
  void drawEye(int cx, int cy, bool isLeft) {
    const int ew = 108, eh = 72;
    const int x = cx - ew / 2, y = cy - eh / 2;

    if (_blinking) {                      // blink: thin white slit
      _canvas.fillRoundRect(x, cy - 6, ew, 12, 6, W);
      return;
    }
    if (_emotion == Emotion::Cool && !isLeft) {   // wink: right eye is a dash
      _canvas.fillRoundRect(x + 10, cy - 4, ew - 20, 9, 4, W);
      return;
    }

    _canvas.fillRoundRect(x, y, ew, eh, 16, W);
    _canvas.fillRect(cx - 9, cy - 9, 18, 18, K);  // fixed pupil at geometric centre

    switch (_emotion) {
      case Emotion::Angry:                 // top slopes DOWN toward the centre
      case Emotion::Confused: {
        int ay = y + (int)(eh * 0.6f);
        if (isLeft) _canvas.fillTriangle(x, y - 2, x + ew, y - 2, x + ew, ay, K);
        else        _canvas.fillTriangle(x + ew, y - 2, x, y - 2, x, ay, K);
        break;
      }
      case Emotion::Sad: {                 // bottom slopes UP toward the centre (droop)
        int sy = y + (int)(eh * 0.45f);
        if (isLeft) _canvas.fillTriangle(x + ew, y + eh + 2, x, y + eh + 2, x + ew, sy, K);
        else        _canvas.fillTriangle(x, y + eh + 2, x + ew, y + eh + 2, x, sy, K);
        break;
      }
      case Emotion::Happy:                 // lower lid up a touch (cheerful squint)
      case Emotion::Excited:
      case Emotion::Love:
        _canvas.fillRect(x, y + eh - (int)(eh * 0.28f), ew, (int)(eh * 0.28f) + 1, K);
        break;
      case Emotion::Sleepy:                // heavy lid: thin slit at the bottom
        _canvas.fillRect(x, y, ew, (int)(eh * 0.62f), K);
        break;
      default: break;                      // Neutral, Thinking, Cool(left eye)
    }
  }

  // --- White teeth grid that bows/pinches per emotion and ripples with audio ---
  void drawTeethMouth() {
    const int mx0 = 100, mx1 = 220, WD = mx1 - mx0;
    const int myc = 158, gap = 16, teeth = 6;

    float phase = millis() / 80.0f;
    float wave = _talking ? (_mouthAmp / 100.0f) * 8.0f : 0.0f;
    float bow = 0.0f, pinch = 0.0f;
    if (!_talking) {
      if (_emotion == Emotion::Happy || _emotion == Emotion::Excited || _emotion == Emotion::Love) bow = -7.0f;
      else if (_emotion == Emotion::Sad || _emotion == Emotion::Sleepy) bow = 7.0f;
      else if (_emotion == Emotion::Angry || _emotion == Emotion::Confused) pinch = 0.45f;  // gritted
    }

    // Column-fill the white band (rounded ends via elliptical taper).
    for (int x = mx0; x <= mx1; x++) {
      float t = (float)(x - mx0) / WD;
      float c = 1.0f - (2 * t - 1) * (2 * t - 1);     // 1 centre .. 0 ends
      float edge = sqrtf(c < 0 ? 0 : c);
      float half = gap * (0.42f + 0.58f * edge) * (1.0f - pinch * c);
      int dy = (int)(bow * c + wave * sinf(x * 0.20f + phase));
      _canvas.drawFastVLine(x, myc - (int)half + dy, (int)(half * 2), W);
    }
    // Black grid: vertical tooth dividers + a horizontal mid-row, both riding the band.
    for (int i = 1; i < teeth; i++) {
      int x = mx0 + (WD * i) / teeth;
      float t = (float)(x - mx0) / WD, c = 1.0f - (2 * t - 1) * (2 * t - 1);
      float half = gap * (0.42f + 0.58f * sqrtf(c < 0 ? 0 : c)) * (1.0f - pinch * c);
      int dy = (int)(bow * c + wave * sinf(x * 0.20f + phase));
      _canvas.drawFastVLine(x, myc - (int)half + dy, (int)(half * 2), K);
    }
    int pT = 0;
    for (int x = mx0; x <= mx1; x++) {
      int dy = (int)(bow * (1.0f - (2 * ((float)(x - mx0) / WD) - 1) * (2 * ((float)(x - mx0) / WD) - 1))
                     + wave * sinf(x * 0.20f + phase));
      if (x > mx0) _canvas.drawLine(x - 1, pT, x, myc + dy, K);
      pT = myc + dy;
    }
  }

  void drawCapsule(int x, int y, int w, int h) {     // concentric white rims, black interior
    int r = h / 2;
    _canvas.fillRoundRect(x, y, w, h, r, W);
    _canvas.fillRoundRect(x + 4, y + 4, w - 8, h - 8, r - 4, K);
    _canvas.drawRoundRect(x + 7, y + 7, w - 14, h - 14, r - 7, W);
  }

  void draw() {
    _canvas.fillSprite(K);

    if (roundLayout()) {
      // SURPRISED: round visor + round white eyes + small "O" mouth.
      _canvas.fillCircle(CXC, 70, 64, W);
      _canvas.fillCircle(CXC, 70, 58, K);
      _canvas.fillCircle(CXC - 30, 66, 26, W);
      _canvas.fillCircle(CXC + 30, 66, 26, W);
      _canvas.fillRect(CXC - 30 - 8, 66 - 8, 16, 16, K);
      _canvas.fillRect(CXC + 30 - 8, 66 - 8, 16, 16, K);
      _canvas.drawCircle(CXC, 150, 16, W);
      _canvas.drawCircle(CXC, 150, 15, W);
      _canvas.pushSprite(0, 0);
      return;
    }

    drawCapsule(22, 22, 276, 92);          // visor
    drawEye(CXC - 62, 66, true);
    drawEye(CXC + 62, 66, false);
    drawTeethMouth();
    _canvas.pushSprite(0, 0);
  }
};

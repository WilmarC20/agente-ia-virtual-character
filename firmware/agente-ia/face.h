// Bender-style face renderer for the ILI9341 (LovyanGFX).
//
// Design: clean geometry, double-buffered in a PSRAM L_GFX_Sprite (no flicker).
// - Visor: large gray horizontal oval with a black border, in the top half.
// - Eyes: yellow rounded rectangles with FIXED black square pupils (never move);
//   mechanical eyelids (gray = visor colour) reshape the eyes per emotion.
// - Mouth: a teeth grid in the lower half whose horizontal lines deform with a
//   sine wave driven by the live audio amplitude (lip-sync); idle, the grid
//   curves per emotion.
//
// Perf: only repaints when something changed (_dirty). Idle = no SPI traffic;
// talking repaints ~25 FPS. Each I2S audio chunk is ~64 ms while a full push is
// ~25 ms, so the ES8311 never underruns. (To push higher FPS, raise freq_write
// in display_setup.h to 80 MHz — lower it back if the panel shows artifacts.)
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
    _canvas.setPsram(true);          // double buffer lives in PSRAM
    _canvas.setColorDepth(16);
    _canvas.createSprite(_gfx.width(), FACE_H);
    _gfx.fillScreen(TFT_BLACK);
  }

  void setEmotion(Emotion e) {
    if (e != _emotion) { _emotion = e; _dirty = true; }
  }

  void setTalking(bool talking) {
    if (talking != _talking) {
      _talking = talking;
      if (!talking) _mouthAmp = 0;
      _dirty = true;
    }
  }

  bool isTalking() const { return _talking; }

  // Live audio loudness (0..100) for the lip-sync mouth.
  void setMouthAmplitude(uint8_t level) {
    _mouthAmp = level > 100 ? 100 : level;
    if (_talking) _dirty = true;
  }

  // Call every loop; repaints only when needed (blink, talk, emotion change).
  void update() {
    uint32_t now = millis();
    if (now >= _nextBlinkAt && !_talking) {
      _blinking = true;
      _blinkUntil = now + 110;
      _nextBlinkAt = now + 3000 + random(3000);
      _dirty = true;
    }
    if (_blinking && now >= _blinkUntil) { _blinking = false; _dirty = true; }
    if (_talking) _dirty = true;     // lip-sync animates each frame
    if (_dirty) { draw(); _dirty = false; }
  }

  // Status text + VU meter live in the strip below the face sprite.
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
    const int h = 8;
    const int w = _gfx.width();
    const int y = _gfx.height() - h;
    int level = (int)(rms * w / 1200);
    if (level > w) level = w;
    uint16_t color = (level > w * 2 / 3) ? TFT_RED : TFT_GREEN;
    _gfx.fillRect(0, y, level, h, color);
    _gfx.fillRect(level, y, w - level, h, 0x2104);
  }

  void clearMicLevel() {
    _gfx.fillRect(0, _gfx.height() - 8, _gfx.width(), 8, TFT_BLACK);
  }

private:
  lgfx::LGFX_Device &_gfx;
  lgfx::LGFX_Sprite _canvas;
  Emotion _emotion = Emotion::Sleepy;
  bool _dirty = true;
  bool _blinking = false;
  bool _talking = false;
  uint8_t _mouthAmp = 0;
  uint32_t _blinkUntil = 0;
  uint32_t _nextBlinkAt = 3000;

  static constexpr int FACE_H = 200;               // sprite height; text strip below
  static constexpr uint16_t VISOR_GRAY = 0xAD55;   // metallic gray (visor + eyelids)
  static constexpr uint16_t EYE_YELLOW = 0xFFE0;
  static constexpr uint16_t MOUTH_DARK = 0x18C3;   // mouth cavity
  static constexpr uint16_t TEETH = 0xFFFF;

  // --- Eyes: yellow rect + fixed square pupil + mechanical eyelids per emotion ---
  void drawEye(int cx, int cy, bool isLeft) {
    const int ew = 74, eh = 84;
    const int x = cx - ew / 2;
    int y = cy - eh / 2;

    if (_blinking) {  // mechanical blink: visor lid drops almost fully
      _canvas.fillRoundRect(x, y, ew, eh, 12, EYE_YELLOW);
      _canvas.fillRect(x, y, ew, eh - 8, VISOR_GRAY);
      return;
    }

    bool wide = (_emotion == Emotion::Surprised || _emotion == Emotion::Dizzy);
    int h = wide ? eh + 8 : eh;
    y = cy - h / 2;
    _canvas.fillRoundRect(x, y, ew, h, 12, EYE_YELLOW);
    _canvas.fillRect(cx - 9, cy - 9, 18, 18, TFT_BLACK);  // fixed pupil, geometric centre

    switch (_emotion) {
      case Emotion::Happy:
      case Emotion::Excited:
      case Emotion::Love:                     // alert/cheerful: lower lid rises 35%
        _canvas.fillRect(x, y + h - (int)(h * 0.35f), ew, (int)(h * 0.35f) + 1, VISOR_GRAY);
        break;
      case Emotion::Sleepy:                   // upper lid covers 75%
        _canvas.fillRect(x, y, ew, (int)(h * 0.75f), VISOR_GRAY);
        break;
      case Emotion::Sad:                      // upper lid 32% (droopy)
        _canvas.fillRect(x, y, ew, (int)(h * 0.32f), VISOR_GRAY);
        break;
      case Emotion::Angry:                    // V frown: diagonal lid from outer-top to centre
        if (isLeft)
          _canvas.fillTriangle(x, y, x + ew, y, x + ew, y + h / 2, VISOR_GRAY);
        else
          _canvas.fillTriangle(x + ew, y, x, y, x, y + h / 2, VISOR_GRAY);
        break;
      case Emotion::Surprised:
      case Emotion::Dizzy:
        break;                                // max open, no lid
      default:                                // Neutral, Thinking, Cool, Confused
        _canvas.fillRect(x, y, ew, (int)(h * 0.15f), VISOR_GRAY);  // straight top lid 15%
        break;
    }
  }

  // --- Mouth: teeth grid; sine-deformed horizontal lines (lip-sync), elastic teeth ---
  void drawMouth() {
    const int mx0 = 98, mx1 = 222, W = mx1 - mx0;
    const int myc = 158, gap = 15;            // teeth row spans myc-gap .. myc+gap
    const int teeth = 7;

    _canvas.fillRoundRect(mx0 - 6, myc - gap - 6, W + 12, gap * 2 + 12, 6, MOUTH_DARK);

    float phase = millis() / 80.0f;
    float waveAmp = _talking ? (_mouthAmp / 100.0f) * 10.0f : 0.0f;  // audio drives the wave
    float bowMax = 0.0f;                       // idle curve per emotion
    if (!_talking) {
      if (_emotion == Emotion::Happy || _emotion == Emotion::Excited || _emotion == Emotion::Love)
        bowMax = -8.0f;                        // smile (centre rises)
      else if (_emotion == Emotion::Sad || _emotion == Emotion::Sleepy)
        bowMax = 8.0f;                         // frown (centre drops)
    }

    int prevTop = 0, prevBot = 0;
    for (int xx = mx0; xx <= mx1; xx++) {
      float t = (float)(xx - mx0) / W;
      float bow = bowMax * (1.0f - (2 * t - 1) * (2 * t - 1));        // parabola, peak at centre
      int dy = (int)(bow + waveAmp * sinf(xx * 0.20f + phase));
      int topY = myc - gap + dy;
      int botY = myc + gap + dy;
      if (xx > mx0) {
        _canvas.drawLine(xx - 1, prevTop, xx, topY, TEETH);
        _canvas.drawLine(xx - 1, prevTop + 1, xx, topY + 1, TEETH);
        _canvas.drawLine(xx - 1, prevBot, xx, botY, TEETH);
        _canvas.drawLine(xx - 1, prevBot + 1, xx, botY + 1, TEETH);
      }
      prevTop = topY; prevBot = botY;
    }
    // vertical teeth connect the two wavy lines elastically (endpoints ride the wave)
    for (int i = 0; i <= teeth; i++) {
      int xx = mx0 + (W * i) / teeth;
      float t = (float)(xx - mx0) / W;
      float bow = bowMax * (1.0f - (2 * t - 1) * (2 * t - 1));
      int dy = (int)(bow + waveAmp * sinf(xx * 0.20f + phase));
      _canvas.drawLine(xx, myc - gap + dy, xx, myc + gap + dy, TEETH);
    }
  }

  void draw() {
    _canvas.fillSprite(TFT_BLACK);
    // Visor: black border ring + gray oval (top half).
    _canvas.fillEllipse(160, 66, 148, 57, TFT_BLACK);
    _canvas.fillEllipse(160, 66, 145, 54, VISOR_GRAY);
    drawEye(108, 66, true);
    drawEye(212, 66, false);
    drawMouth();
    _canvas.pushSprite(0, 0);
  }
};

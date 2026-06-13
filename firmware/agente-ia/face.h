// Animated face renderer: draws the character's emotions on the display.
// Landscape layout (320x240): face on top, status/reply text at the bottom.
#pragma once

#include <math.h>
#include <LovyanGFX.hpp>

enum class Emotion { Neutral, Happy, Sad, Angry, Surprised, Thinking, Sleepy };

inline Emotion emotionFromString(const String &s) {
  if (s == "happy") return Emotion::Happy;
  if (s == "sad") return Emotion::Sad;
  if (s == "angry") return Emotion::Angry;
  if (s == "surprised") return Emotion::Surprised;
  if (s == "thinking") return Emotion::Thinking;
  if (s == "sleepy") return Emotion::Sleepy;
  return Emotion::Neutral;
}

class Face {
public:
  explicit Face(lgfx::LGFX_Device &gfx) : _gfx(gfx), _canvas(&gfx) {}

  void begin() {
    _canvas.setColorDepth(16);
    _canvas.createSprite(_gfx.width(), 180);
  }

  void setEmotion(Emotion e) {
    if (e != _emotion) {
      _emotion = e;
      _dirty = true;
    }
  }

  void setTalking(bool talking) {
    if (talking != _talking) {
      _talking = talking;
      if (!talking) _mouthAmp = 0;  // close mouth when speech ends
      _dirty = true;
    }
  }

  bool isTalking() const { return _talking; }

  // Drive the lip-sync mouth from the live audio loudness (0..100).
  // Called from speak() with each played chunk's peak amplitude.
  void setMouthAmplitude(uint8_t level) {
    _mouthAmp = level > 100 ? 100 : level;
    if (_talking) _dirty = true;
  }

  // Call frequently from loop(); handles blinking, talking mouth, redraws.
  void update() {
    uint32_t now = millis();
    if (_talking) {
      _dirty = true;
    }
    if (now >= _nextBlinkAt && !_talking) {
      _blinking = true;
      _blinkUntil = now + 120;
      _nextBlinkAt = now + 2500 + random(2500);
      _dirty = true;
    }
    if (_blinking && now >= _blinkUntil) {
      _blinking = false;
      _dirty = true;
    }
    if (_dirty) {
      draw();
      _dirty = false;
    }
  }

  // VU meter: thin bar along the bottom edge showing mic input level.
  void drawMicLevel(uint32_t rms) {
    const int h = 8;
    const int w = _gfx.width();
    const int y = _gfx.height() - h;
    int level = (int)(rms * w / 1200);
    if (level > w) level = w;
    uint16_t color = (level > w * 2 / 3) ? TFT_RED : TFT_GREEN;
    _gfx.fillRect(0, y, level, h, color);
    _gfx.fillRect(level, y, w - level, h, 0x2104 /* dark gray track */);
  }

  void clearMicLevel() {
    _gfx.fillRect(0, _gfx.height() - 8, _gfx.width(), 8, TFT_BLACK);
  }

  void showText(const String &text, uint16_t color = TFT_WHITE) {
    int y = 185;
    _gfx.fillRect(0, y, _gfx.width(), _gfx.height() - y, TFT_BLACK);
    _gfx.setTextColor(color, TFT_BLACK);
    _gfx.setTextSize(1);
    _gfx.setFont(&fonts::DejaVu18);
    _gfx.setCursor(8, y + 6);
    _gfx.setTextWrap(true);
    _gfx.print(text);
  }

private:
  lgfx::LGFX_Device &_gfx;
  lgfx::LGFX_Sprite _canvas;
  Emotion _emotion = Emotion::Sleepy;
  bool _dirty = true;
  bool _blinking = false;
  bool _talking = false;
  uint8_t _mouthAmp = 0;  // live audio loudness while talking (0..100)
  uint32_t _blinkUntil = 0;
  uint32_t _nextBlinkAt = 3000;

  static constexpr uint16_t FACE_COLOR = TFT_CYAN;

  int mouthCenterY() const {
    switch (_emotion) {
      case Emotion::Happy: return 138;
      case Emotion::Sad: return 175;
      case Emotion::Surprised: return 145;
      case Emotion::Angry: return 150;
      case Emotion::Thinking: return 148;
      case Emotion::Sleepy: return 150;
      default: return 148;
    }
  }

  void drawAnimatedMouth(int cx, int cy) {
    // Real lip-sync: the mouth opens proportionally to the live audio loudness
    // (_mouthAmp, fed from speak()). A little vibration keeps it lively.
    float time = millis() / 22.0f;
    cx += (int)(1.5f * sinf(time * 2.1f));

    _canvas.fillRect(cx - 52, cy - 26, 104, 48, TFT_BLACK);  // clear mouth area

    const int halfW = 26;
    int openH = 4 + (_mouthAmp * 22) / 100;  // 4 px (closed) .. 26 px (wide open)

    // Outer lips
    _canvas.fillRoundRect(cx - halfW, cy - openH / 2, halfW * 2, openH, openH / 2, FACE_COLOR);
    // Dark cavity once the mouth is open enough (reads as an open mouth)
    if (openH >= 12) {
      int ih = openH - 8;
      _canvas.fillRoundRect(cx - halfW + 5, cy - ih / 2, (halfW - 5) * 2, ih, ih / 2, TFT_BLACK);
    }

    // Side equalizer bars react to the same loudness
    const int barCount = 4;
    for (int i = 0; i < barCount; i++) {
      float ph = time * 1.6f + i * 0.85f;
      int h = 2 + (int)((2 + _mouthAmp / 6) * (0.5f + 0.5f * sinf(ph)));
      _canvas.fillRect(cx - 46 + i * 4, cy - h / 2, 3, h, FACE_COLOR);
      _canvas.fillRect(cx + 34 + i * 4, cy - h / 2, 3, h, FACE_COLOR);
    }
  }

  void draw() {
    _canvas.fillSprite(TFT_BLACK);
    int cx = _canvas.width() / 2;
    int eyeY = 80;
    int eyeDX = 60;

    switch (_emotion) {
      case Emotion::Neutral:
        drawEyes(cx, eyeY, eyeDX, 24, 30);
        if (!_talking) _canvas.fillRoundRect(cx - 30, 145, 60, 8, 4, FACE_COLOR);
        break;
      case Emotion::Happy:
        drawEyes(cx, eyeY, eyeDX, 24, 30);
        if (!_talking) smileArc(cx, 130, 45, false);
        break;
      case Emotion::Sad:
        drawEyes(cx, eyeY + 8, eyeDX, 22, 24);
        if (!_talking) smileArc(cx, 175, 40, true);
        break;
      case Emotion::Angry:
        drawEyes(cx, eyeY + 5, eyeDX, 24, 22);
        drawBrow(cx - eyeDX, eyeY - 30, true);
        drawBrow(cx + eyeDX, eyeY - 30, false);
        if (!_talking) _canvas.fillRoundRect(cx - 30, 150, 60, 8, 4, FACE_COLOR);
        break;
      case Emotion::Surprised:
        drawEyes(cx, eyeY, eyeDX, 28, 36);
        if (!_talking) {
          _canvas.fillEllipse(cx, 145, 18, 24, FACE_COLOR);
          _canvas.fillEllipse(cx, 145, 12, 17, TFT_BLACK);
        }
        break;
      case Emotion::Thinking: {
        drawEyes(cx, eyeY, eyeDX, 22, 26);
        if (!_talking) _canvas.fillRoundRect(cx - 25, 148, 42, 7, 3, FACE_COLOR);
        int phase = (millis() / 350) % 3;
        for (int i = 0; i <= phase; i++)
          _canvas.fillCircle(cx + 65 + i * 16, 40 - i * 10, 5, FACE_COLOR);
        _dirty = true;
        break;
      }
      case Emotion::Sleepy:
        _canvas.fillRoundRect(cx - eyeDX - 22, eyeY, 44, 7, 3, FACE_COLOR);
        _canvas.fillRoundRect(cx + eyeDX - 22, eyeY, 44, 7, 3, FACE_COLOR);
        _canvas.setTextColor(FACE_COLOR);
        _canvas.setFont(&fonts::DejaVu24);
        _canvas.setCursor(cx + 85, 30);
        _canvas.print("z");
        _canvas.setCursor(cx + 100, 12);
        _canvas.print("Z");
        if (!_talking) _canvas.fillRoundRect(cx - 22, 150, 44, 7, 3, FACE_COLOR);
        break;
    }

    if (_talking) {
      drawAnimatedMouth(cx, mouthCenterY());
    }

    _canvas.pushSprite(0, 0);
  }

  void drawEyes(int cx, int y, int dx, int rx, int ry) {
    if (_blinking) {
      _canvas.fillRoundRect(cx - dx - rx, y - 3, rx * 2, 7, 3, FACE_COLOR);
      _canvas.fillRoundRect(cx + dx - rx, y - 3, rx * 2, 7, 3, FACE_COLOR);
    } else {
      _canvas.fillEllipse(cx - dx, y, rx, ry, FACE_COLOR);
      _canvas.fillEllipse(cx + dx, y, rx, ry, FACE_COLOR);
    }
  }

  void smileArc(int cx, int cy, int r, bool frown) {
    int start = frown ? 200 : 20;
    int end = frown ? 340 : 160;
    _canvas.fillArc(cx, cy, r, r - 8, start, end, FACE_COLOR);
  }

  void drawBrow(int x, int y, bool left) {
    int x0 = x - 24, x1 = x + 24;
    int yInner = left ? y + 12 : y - 0;
    int yOuter = left ? y - 0 : y + 12;
    for (int t = 0; t < 6; t++)
      _canvas.drawLine(x0, yOuter + t, x1, yInner + t, FACE_COLOR);
  }
};

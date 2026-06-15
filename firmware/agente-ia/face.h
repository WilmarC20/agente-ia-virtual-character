// Animated face renderer: draws the character's emotions on the display.
// Landscape layout (320x240): face on top, status/reply text at the bottom.
// Full-color: each emotion has its own colour; some add animated flourishes.
#pragma once

#include <math.h>
#include <LovyanGFX.hpp>

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
  void setMouthAmplitude(uint8_t level) {
    _mouthAmp = level > 100 ? 100 : level;
    if (_talking) _dirty = true;
  }

  // Call frequently from loop(); drives blinking, wandering gaze, breathing, mouth.
  void update() {
    uint32_t now = millis();
    updateGaze(now);
    if (now >= _nextBlinkAt && !_talking) {
      _blinking = true;
      _blinkUntil = now + 120;
      _nextBlinkAt = now + 2500 + random(2500);
    }
    if (_blinking && now >= _blinkUntil) _blinking = false;
    // Always redraw: the breathing bob + wandering gaze keep the face alive.
    draw();
    _dirty = false;
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
  uint8_t _mouthAmp = 0;
  uint32_t _blinkUntil = 0;
  uint32_t _nextBlinkAt = 3000;
  uint16_t _color = TFT_CYAN;  // active emotion colour, set each draw()
  // Idle life: eyes wander (gaze) and the whole face "breathes".
  int _gx = 0, _gy = 0;        // eased gaze offset
  int _gtx = 0, _gty = 0;      // gaze target
  uint32_t _nextGazeAt = 1500;
  int _offX = 0, _offY = 0;    // per-frame face offset (gaze + breathing)

  // RGB565 palette for soft tones the TFT_* macros don't cover.
  static constexpr uint16_t COL_SADBLUE = 0x5CFF;
  static constexpr uint16_t COL_LAVENDER = 0x8C9F;
  static constexpr uint16_t COL_PINK = 0xFC3F;
  static constexpr uint16_t COL_PURPLE = 0xA81F;

  uint16_t colorFor(Emotion e) const {
    switch (e) {
      case Emotion::Happy:     return TFT_GREEN;
      case Emotion::Sad:       return COL_SADBLUE;
      case Emotion::Angry:     return TFT_RED;
      case Emotion::Surprised: return TFT_YELLOW;
      case Emotion::Thinking:  return TFT_CYAN;
      case Emotion::Sleepy:    return COL_LAVENDER;
      case Emotion::Love:      return COL_PINK;
      case Emotion::Excited:   return TFT_ORANGE;
      case Emotion::Cool:      return TFT_CYAN;
      case Emotion::Confused:  return TFT_ORANGE;
      case Emotion::Dizzy:     return COL_PURPLE;
      default:                 return TFT_CYAN;  // Neutral
    }
  }

  // Eyes wander: pick a new gaze target now and then, ease toward it each frame.
  void updateGaze(uint32_t now) {
    if (now >= _nextGazeAt) {
      if (random(100) < 38) {           // often recenter
        _gtx = 0; _gty = 0;
      } else {                          // glance somewhere
        _gtx = random(-9, 10);
        _gty = random(-5, 6);
      }
      _nextGazeAt = now + 700 + random(1800);
    }
    if (_gx < _gtx) _gx++; else if (_gx > _gtx) _gx--;
    if (_gy < _gty) _gy++; else if (_gy > _gty) _gy--;
  }

  int mouthCenterY() const {
    switch (_emotion) {
      case Emotion::Happy:     return 138;
      case Emotion::Love:      return 138;
      case Emotion::Excited:   return 140;
      case Emotion::Sad:       return 175;
      case Emotion::Surprised: return 145;
      default:                 return 148;
    }
  }

  void draw() {
    _canvas.fillSprite(TFT_BLACK);
    _color = colorFor(_emotion);
    int cx = _canvas.width() / 2;
    int eyeY = 80;
    int eyeDX = 60;
    // Breathing bob (whole face) + wandering gaze (eyes only).
    int bob = (int)(2.5f * sinf(millis() / 1100.0f));
    _offX = _gx;          // applied to eyes inside drawEyes()/eye helpers
    _offY = _gy + bob;
    int ey = eyeY + bob;  // eye baseline rides the breath; gaze added per-helper

    switch (_emotion) {
      case Emotion::Neutral:
        drawEyes(cx, eyeY, eyeDX, 24, 30);
        if (!_talking) _canvas.fillRoundRect(cx - 30, 145 + bob, 60, 8, 4, _color);
        break;
      case Emotion::Happy:
        drawEyes(cx, eyeY, eyeDX, 24, 30);
        if (!_talking) smileArc(cx, 130 + bob, 45, false);
        break;
      case Emotion::Sad:
        drawEyes(cx, eyeY + 8, eyeDX, 22, 24);
        if (!_talking) smileArc(cx, 175 + bob, 40, true);
        drawTear(cx - eyeDX - 14 + _offX, ey + 18);
        break;
      case Emotion::Angry:
        drawEyes(cx, eyeY + 5, eyeDX, 24, 22);
        drawBrow(cx - eyeDX + _offX, ey - 30, true);
        drawBrow(cx + eyeDX + _offX, ey - 30, false);
        if (!_talking) _canvas.fillRoundRect(cx - 30, 150 + bob, 60, 8, 4, _color);
        break;
      case Emotion::Surprised:
        drawEyes(cx, eyeY, eyeDX, 28, 36);
        if (!_talking) {
          _canvas.fillEllipse(cx, 145 + bob, 18, 24, _color);
          _canvas.fillEllipse(cx, 145 + bob, 12, 17, TFT_BLACK);
        }
        break;
      case Emotion::Thinking: {
        drawEyes(cx, eyeY, eyeDX, 22, 26);
        if (!_talking) _canvas.fillRoundRect(cx - 25, 148 + bob, 42, 7, 3, _color);
        int phase = (millis() / 350) % 3;
        for (int i = 0; i <= phase; i++)
          _canvas.fillCircle(cx + 65 + i * 16, 40 - i * 10, 5, _color);
        break;
      }
      case Emotion::Sleepy:
        _canvas.fillRoundRect(cx - eyeDX - 22 + _offX, ey, 44, 7, 3, _color);
        _canvas.fillRoundRect(cx + eyeDX - 22 + _offX, ey, 44, 7, 3, _color);
        _canvas.setTextColor(_color);
        _canvas.setFont(&fonts::DejaVu24);
        _canvas.setCursor(cx + 85, 30 + bob);
        _canvas.print("z");
        _canvas.setCursor(cx + 100, 12 + bob);
        _canvas.print("Z");
        if (!_talking) _canvas.fillRoundRect(cx - 22, 150 + bob, 44, 7, 3, _color);
        break;
      case Emotion::Love:
        drawHeart(cx - eyeDX + _offX, ey, 16, _color);
        drawHeart(cx + eyeDX + _offX, ey, 16, _color);
        if (!_talking) smileArc(cx, 128 + bob, 45, false);
        drawFloatingHearts(cx);
        break;
      case Emotion::Excited:
        drawStar(cx - eyeDX + _offX, ey, 24, _color);
        drawStar(cx + eyeDX + _offX, ey, 24, _color);
        if (!_talking) _canvas.fillArc(cx, 128 + bob, 48, 30, 20, 160, _color);  // big grin
        drawSparkles(cx);
        break;
      case Emotion::Cool:
        drawSunglasses(cx + _offX, ey, eyeDX);
        if (!_talking) _canvas.fillArc(cx + 6, 150 + bob, 34, 27, 10, 95, _color);  // smirk
        break;
      case Emotion::Confused:
        _canvas.fillEllipse(cx - eyeDX + _offX, ey + 4, 22, 26, _color);              // normal eye
        _canvas.fillRoundRect(cx + eyeDX - 22 + _offX, ey - 2, 44, 8, 4, _color);     // squint eye
        drawBrow(cx + eyeDX + _offX, ey - 34, false);                                 // raised brow
        if (!_talking) drawSquiggle(cx - 22, 152 + bob, 44);                          // puzzled mouth
        drawQuestion(cx + 70, 30);
        break;
      case Emotion::Dizzy: {
        int wob = (int)(4 * sinf(millis() / 120.0f));
        drawXEye(cx - eyeDX + wob, ey, 18);
        drawXEye(cx + eyeDX + wob, ey, 18);
        if (!_talking) drawSquiggle(cx - 24 + wob, 152 + bob, 48);
        break;
      }
    }

    if (_talking) {
      drawAnimatedMouth(cx, mouthCenterY() + bob);
    }

    _canvas.pushSprite(0, 0);
  }

  // Eyes carry the gaze (_offX/_offY) so they wander while the face breathes.
  void drawEyes(int cx, int y, int dx, int rx, int ry) {
    cx += _offX;
    y += _offY;
    if (_blinking) {
      _canvas.fillRoundRect(cx - dx - rx, y - 3, rx * 2, 7, 3, _color);
      _canvas.fillRoundRect(cx + dx - rx, y - 3, rx * 2, 7, 3, _color);
    } else {
      _canvas.fillEllipse(cx - dx, y, rx, ry, _color);
      _canvas.fillEllipse(cx + dx, y, rx, ry, _color);
    }
  }

  void smileArc(int cx, int cy, int r, bool frown) {
    int start = frown ? 200 : 20;
    int end = frown ? 340 : 160;
    _canvas.fillArc(cx, cy, r, r - 8, start, end, _color);
  }

  void drawBrow(int x, int y, bool left) {
    int x0 = x - 24, x1 = x + 24;
    int yInner = left ? y + 12 : y - 0;
    int yOuter = left ? y - 0 : y + 12;
    for (int t = 0; t < 6; t++)
      _canvas.drawLine(x0, yOuter + t, x1, yInner + t, _color);
  }

  void drawHeart(int cx, int cy, int s, uint16_t col) {
    _canvas.fillCircle(cx - s / 2, cy - s / 2, s / 2 + 1, col);
    _canvas.fillCircle(cx + s / 2, cy - s / 2, s / 2 + 1, col);
    _canvas.fillTriangle(cx - s, cy - s / 4, cx + s, cy - s / 4, cx, cy + s, col);
  }

  // 4-point sparkle / star.
  void drawStar(int cx, int cy, int r, uint16_t col) {
    int t = r / 3;
    _canvas.fillTriangle(cx, cy - r, cx - t, cy, cx + t, cy, col);
    _canvas.fillTriangle(cx, cy + r, cx - t, cy, cx + t, cy, col);
    _canvas.fillTriangle(cx - r, cy, cx, cy - t, cx, cy + t, col);
    _canvas.fillTriangle(cx + r, cy, cx, cy - t, cx, cy + t, col);
  }

  void drawTear(int x, int y) {
    _canvas.fillCircle(x, y, 5, COL_SADBLUE);
    _canvas.fillTriangle(x - 5, y, x + 5, y, x, y - 9, COL_SADBLUE);
  }

  void drawSunglasses(int cx, int y, int dx) {
    // two dark lenses with a coloured frame + bridge
    _canvas.fillRoundRect(cx - dx - 30, y - 18, 56, 38, 8, TFT_BLACK);
    _canvas.fillRoundRect(cx + dx - 26, y - 18, 56, 38, 8, TFT_BLACK);
    _canvas.drawRoundRect(cx - dx - 30, y - 18, 56, 38, 8, _color);
    _canvas.drawRoundRect(cx + dx - 26, y - 18, 56, 38, 8, _color);
    _canvas.fillRect(cx - dx + 26, y - 6, dx - 26, 5, _color);  // bridge
    _canvas.fillRect(cx - 30, y - 12, 10, 4, TFT_WHITE);        // glint
  }

  void drawXEye(int x, int y, int r) {
    for (int t = -1; t <= 1; t++) {
      _canvas.drawLine(x - r, y - r + t, x + r, y + r + t, _color);
      _canvas.drawLine(x - r, y + r + t, x + r, y - r + t, _color);
    }
  }

  void drawSquiggle(int x, int y, int w) {
    int prev = y;
    for (int i = 1; i <= w; i++) {
      int yy = y + (int)(4 * sinf(i * 0.5f));
      _canvas.drawLine(x + i - 1, prev, x + i, yy, _color);
      _canvas.drawLine(x + i - 1, prev + 1, x + i, yy + 1, _color);
      prev = yy;
    }
  }

  void drawQuestion(int x, int y) {
    int bob = (int)(4 * sinf(millis() / 250.0f));
    _canvas.setTextColor(_color);
    _canvas.setFont(&fonts::DejaVu40);
    _canvas.setCursor(x, y + bob);
    _canvas.print("?");
  }

  void drawFloatingHearts(int cx) {
    for (int i = 0; i < 3; i++) {
      int span = 70;
      int rise = (millis() / 16 + i * 240) % (span * 10) / 10;  // 0..span, looping
      int hx = cx + (i == 1 ? 92 : -92) + (i == 2 ? 30 : 0);
      int hy = 150 - rise;
      if (hy > 4) drawHeart(hx, hy, 7, COL_PINK);
    }
  }

  void drawSparkles(int cx) {
    int phase = (millis() / 200) % 4;
    const int sx[4] = {30, 290, 60, 260};
    const int sy[4] = {30, 36, 120, 110};
    for (int i = 0; i < 4; i++)
      if (i != phase) drawStar(sx[i], sy[i], 6 + (i == phase ? 4 : 0), TFT_YELLOW);
  }

  void drawAnimatedMouth(int cx, int cy) {
    // Bender mouth: fixed teeth grid; the two inner lines ripple like a voice wave.
    _canvas.fillRect(cx - 56, cy - 30, 112, 60, TFT_BLACK);
    const int halfW = 42;
    const int hOuter = 15;
    const int hInner = 5;
    const float k = 0.30f;
    float phase = millis() / 90.0f;
    float amp = 1.0f + (_mouthAmp * 7) / 100.0f;
    float ampE = amp * 0.22f;

    int x0 = cx - halfW;
    int pT = cy - hOuter + (int)(ampE * sinf((x0 - cx) * k + phase));
    int pB = cy + hOuter + (int)(ampE * sinf((x0 - cx) * k + phase));
    int pU = cy - hInner + (int)(amp * sinf((x0 - cx) * k + phase));
    int pL = cy + hInner + (int)(amp * sinf((x0 - cx) * k + phase + 0.7f));
    for (int x = x0 + 1; x <= cx + halfW; x++) {
      int yT = cy - hOuter + (int)(ampE * sinf((x - cx) * k + phase));
      int yB = cy + hOuter + (int)(ampE * sinf((x - cx) * k + phase));
      int yU = cy - hInner + (int)(amp * sinf((x - cx) * k + phase));
      int yL = cy + hInner + (int)(amp * sinf((x - cx) * k + phase + 0.7f));
      _canvas.drawLine(x - 1, pT, x, yT, _color);
      _canvas.drawLine(x - 1, pB, x, yB, _color);
      _canvas.drawLine(x - 1, pU, x, yU, _color);
      _canvas.drawLine(x - 1, pL, x, yL, _color);
      pT = yT; pB = yB; pU = yU; pL = yL;
    }
    const int teeth = 8;
    for (int i = 0; i <= teeth; i++) {
      int x = cx - halfW + (halfW * 2 * i) / teeth;
      int w = (int)(ampE * sinf((x - cx) * k + phase));
      _canvas.drawLine(x, cy - hOuter + w, x, cy + hOuter + w, _color);
    }
  }
};

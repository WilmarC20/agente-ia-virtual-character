// Capsule visor face (LovyanGFX). Dark CRT/LED aesthetic: glow, scanlines,
// lit eyes with catchlight pupils, Bender-style LED segment mouth at neutral.
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
    if (!_canvas.createSprite(FACE_DESIGN_W, FACE_H)) {
      Serial.println("FACE: createSprite failed");
    }
    _canvas.setPivot(FACE_DESIGN_W * 0.5f, FACE_H * 0.5f);
    _faceScreenW = _gfx.width();
    _gfx.fillScreen(TFT_BLACK);
    _browPhase = random(0, 628) / 100.0f;
    _nextBlinkAt = millis() + 2800 + random(3700);
  }

  void setEmotion(Emotion e) {
    if (e != _emotion) {
      _emotion = e;
      _gazeX = _gazeY = 0;
      _gazeTx = _gazeTy = 0;
      _nextGazeAt = millis() + 700 + random(1800);
      _dirty = true;
    }
  }

  void setBored(bool b) { if (b != _bored) { _bored = b; _dirty = true; } }
  void setShowGear(bool v) { if (v != _showGear) { _showGear = v; _dirty = true; } }
  void redraw() { _dirty = true; }

  static bool gearHit(int sx, int sy, int screenW) {
    if (screenW <= 250) {
      const int gx = 227, gy = 16;
      int dx = sx - gx, dy = sy - gy;
      return (sx >= 196 && sy <= 64) || ((dx * dx + dy * dy) <= (20 * 20));
    }
    int dx = sx - 303, dy = sy - 16;
    return (sx >= 262 && sy <= 64) || ((dx * dx + dy * dy) <= (20 * 20));
  }

  void setTalking(bool talking) {
    if (talking != _talking) {
      _talking = talking;
      if (talking) { _gazeX = _gazeY = 0; }
      else { _mouthAmp = 0; _mouthAmpTarget = 0; _mouthAmpSmooth = 0; _singing = false; _nextGazeAt = millis() + 1200; }
      _dirty = true;
    }
  }
  bool isTalking() const { return _talking; }

  void setSinging(bool singing) {
    if (singing != _singing) {
      _singing = singing;
      if (singing) _mouthAmp = 100;
      _dirty = true;
    }
  }
  bool isSinging() const { return _singing; }

  void setMouthAmplitude(uint8_t level) {
    _mouthAmpTarget = level > 100 ? 100 : level;
    if (_talking || _singing) _dirty = true;
  }

  void smoothMouthAmp() {
    if (_singing) {
      _mouthAmpSmooth = 100.0f;
      _mouthAmp = 100;
      return;
    }
    if (!_talking) {
      _mouthAmpSmooth = 0.0f;
      _mouthAmpTarget = 0;
      _mouthAmp = 0;
      return;
    }
    float target = (float)_mouthAmpTarget;
    float gap = fabsf(target - _mouthAmpSmooth);
    float rate = (target > _mouthAmpSmooth)
      ? (0.88f + fminf(0.1f, gap * 0.009f))
      : (0.55f + fminf(0.4f, gap * 0.014f));
    _mouthAmpSmooth += (target - _mouthAmpSmooth) * rate;
    if (_mouthAmpSmooth < 0.06f) _mouthAmpSmooth = 0.0f;
    _mouthAmp = (uint8_t)(_mouthAmpSmooth + 0.5f);
  }

  void update() {
    uint32_t now = millis();
    bool changed = false;

    if (!_talking && !_singing && now >= _nextBlinkAt && now >= _blinkEndAt) {
      _blinkEndAt = now + 110 + random(90);
      _nextBlinkAt = _blinkEndAt + blinkDelayMs();
      changed = true;
    }
    float blink = 0;
    if (now < _blinkEndAt) {
      float u = (_blinkEndAt - now) / 200.0f;
      float bell = 1.0f - fminf(1.0f, fabsf(u - 0.5f) * 2.0f);
      blink = fminf(1.0f, bell * 1.2f);
      if (blink != _blinkAmt) changed = true;
    } else if (_blinkAmt > 0) {
      changed = true;
    }
    _blinkAmt = blink;

    if (!_talking && !_singing && _blinkAmt < 0.05f) {
      if (_emotion == Emotion::Thinking) {
        int g = ((now / 320) % 2) ? 10 : -10;
        if (g != (int)_gazeTx) { _gazeTx = g; _gazeTy = -3; changed = true; }
      } else if (now >= _nextGazeAt) {
        int amp = gazeAmp();
        _gazeTx = random(-amp, amp + 1);
        _gazeTy = random(-(amp / 4), amp / 4 + 1);
        _nextGazeAt = now + 850 + random(2300);
        changed = true;
      }
      float nx = _gazeX + (_gazeTx - _gazeX) * 0.18f;
      float ny = _gazeY + (_gazeTy - _gazeY) * 0.14f;
      if (fabsf(nx - _gazeX) > 0.3f || fabsf(ny - _gazeY) > 0.3f) {
        _gazeX = nx; _gazeY = ny;
        changed = true;
      }
    }

    if (_talking || _singing) {
      smoothMouthAmp();
      _dirty = true;
    } else if (changed) _dirty = true;

    if (_dirty) { draw(); _dirty = false; }
  }

  void showText(const String &text, uint16_t color = TFT_WHITE) {
    int y = FACE_OFFSET_Y + FACE_H + 2;
    _gfx.fillRect(0, y, _gfx.width(), _gfx.height() - y, TFT_BLACK);
    if (text.length() == 0) return;
    int tx = 8 + FACE_OFFSET_X;
    if (tx < 2) tx = 2;
    _gfx.setTextColor(color, TFT_BLACK);
    _gfx.setFont(&fonts::DejaVu18);
    _gfx.setCursor(tx, y + 4);
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
  bool _dirty = true, _talking = false, _singing = false, _showGear = false, _bored = false;
  uint8_t _mouthAmp = 0, _mouthAmpTarget = 0;
  float _mouthAmpSmooth = 0;
  uint32_t _blinkEndAt = 0, _nextBlinkAt = 5000, _nextGazeAt = 4000;
  int _faceScreenW = FACE_DESIGN_W;
  float _blinkAmt = 0, _gazeX = 0, _gazeY = 0, _gazeTx = 0, _gazeTy = 0, _browPhase = 0;

  static constexpr int FACE_DESIGN_W = 320;
  static constexpr int FACE_H = 200;
  // Panel has dead space on its right edge; shift the whole face left so it
  // sits centered inside the visible area.
  static constexpr int FACE_OFFSET_X = -20;
  // The 200px-tall face used to sit at the top of the 240px screen, leaving a
  // big black gap below. Push it down so the face is vertically centered; the
  // status text still fits just below the sprite (no overlap, no flicker).
  static constexpr int FACE_OFFSET_Y = 12;
  static constexpr int EYE_L = 100, EYE_R = 220, EYE_Y = 68, EYE_RAD = 34;
  static constexpr int CXC = 160;
  static constexpr float MOUTH_H_MUL = 1.22f;

  // Dark theme: black page, glowing visor panel, lit eyes + LED-segment mouth.
  static constexpr uint16_t BG = 0x0000;
  static constexpr uint16_t VISOR = 0x10A2;
  static constexpr uint16_t VISOR_DEEP = 0x0841;
  static constexpr uint16_t SCANLINE = 0x0821;
  static constexpr uint16_t INK = 0x07FF;
  static constexpr uint16_t INK_GLOW = 0x0318;
  static constexpr uint16_t INK_BRIGHT = 0x57FF;
  static constexpr uint16_t EYE = 0xDEFB;       // warm white (not flat 0xFFFF)
  static constexpr uint16_t EYE_GLOW = 0x2945;
  static constexpr uint16_t PUP = 0x0000;
  static constexpr uint16_t HILITE = 0xFFFF;
  static constexpr uint16_t RED = 0xE800;      // love hearts
  static constexpr uint16_t BLUE = 0x2D5F;      // sad tear
  static constexpr uint16_t PURPLE = 0x780F;    // dizzy spiral
  static constexpr uint16_t EYE_LOVE = 0xFFDF;  // warm white
  static constexpr uint16_t EYE_EXCITED = 0xFFF7;
  static constexpr uint16_t GEAR = 0x5C9F;

  enum class Brow { Flat, Arch, Angry, Sad, Raise, Asym };
  enum class LidAng { None, AngryIn, SadOut };
  enum class Pupil { Square, Heart, Spiral, X };
  enum class MouthKind { Grid, Compact, O };

  struct MouthCfg {
    int x0, x1, gap, teeth, rows;
    float bow, pinch, tilt, glitch;
  };

  struct Preset {
    Brow brow;
    LidAng lidAng;
    Pupil pupil;
    MouthKind mouthKind;
    MouthCfg mouth;
    float topLid, botLid, topLidL, topLidR, botLidL, botLidR;
    int eyeR, pupilS, pupilOffX, pupilOffY;
    uint16_t eyeTint;
    bool tear, winkRight, winkLeft;
  };

  int gazeAmp() const {
    switch (_emotion) {
      case Emotion::Sleepy: return 2;
      case Emotion::Thinking: return 10;
      case Emotion::Happy: case Emotion::Excited: return 7;
      case Emotion::Angry: return 4;
      default: return 6;
    }
  }

  uint32_t blinkDelayMs() const {
    switch (_emotion) {
      case Emotion::Sleepy: return 1200 + random(1400);
      case Emotion::Thinking: return 2600 + random(2600);
      case Emotion::Happy: case Emotion::Excited: return 2200 + random(2000);
      case Emotion::Angry: return 3400 + random(3600);
      default: return 2800 + random(3700);
    }
  }

  Preset preset() const {
    Preset p{};
    p.eyeR = EYE_RAD;
    p.pupil = Pupil::Square;
    p.pupilS = 6;
    p.mouthKind = MouthKind::Grid;
    p.mouth = {88, 232, 19, 4, 3, 2, 0, 0, 0};
    p.brow = Brow::Flat;
    p.lidAng = LidAng::None;
    p.topLidL = p.topLidR = -1;

    switch (_emotion) {
      case Emotion::Happy:
        p.brow = Brow::Arch; p.topLid = 0.08f; p.botLid = 0.22f;
        p.mouth = {82, 238, 23, 7, 3, 9, 0, 0, 0};
        break;
      case Emotion::Sad:
        p.brow = Brow::Sad; p.lidAng = LidAng::SadOut; p.topLid = 0.05f; p.tear = true;
        p.mouth = {100, 220, 16, 8, 3, -11, 0, 0, 0};
        break;
      case Emotion::Angry:
        p.brow = Brow::Angry; p.lidAng = LidAng::AngryIn; p.topLid = p.botLid = 0.38f;
        p.mouth = {88, 232, 17, 6, 3, -4, 0.5f, 0, 0};
        break;
      case Emotion::Surprised:
        p.brow = Brow::Raise; p.eyeR = 38; p.pupilS = 7; p.mouthKind = MouthKind::Compact;
        break;
      case Emotion::Thinking:
        p.brow = Brow::Asym; p.topLid = 0.18f; p.pupilOffX = 8; p.pupilOffY = -5;
        p.mouth = {96, 224, 17, 8, 3, 0, 0, 0, 0};
        break;
      case Emotion::Sleepy:
        p.topLid = 0.55f;
        p.mouth = {96, 224, 15, 8, 3, -5, 0, 0, 0};
        break;
      case Emotion::Love:
        p.brow = Brow::Arch; p.eyeR = 36; p.botLid = 0.12f;
        p.pupil = Pupil::Heart; p.pupilS = 15; p.eyeTint = EYE_LOVE;
        p.mouth = {84, 236, 21, 7, 3, 7, 0, 0, 0};
        break;
      case Emotion::Excited:
        p.brow = Brow::Raise; p.eyeR = 37; p.pupilS = 8; p.eyeTint = EYE_EXCITED;
        p.mouth = {78, 242, 25, 8, 3, 11, 0, 0, 0};
        break;
      case Emotion::Cool:
        p.brow = Brow::Arch; p.winkRight = true; p.topLid = 0.1f;
        p.mouth = {88, 232, 19, 7, 3, 3, 0, 0, 8};
        break;
      case Emotion::Confused:
        p.brow = Brow::Asym; p.topLidL = 0.28f; p.topLidR = 0.1f;
        p.mouth = {92, 228, 18, 6, 3, 0, 0.25f, 7, 0};
        break;
      case Emotion::Dizzy:
        p.pupil = Pupil::Spiral;
        p.mouth = {88, 232, 19, 6, 3, 0, 0, 0, 5};
        break;
      default: // Neutral
        p.topLid = 0.12f;
        p.mouth = {88, 232, 19, 4, 3, 2, 0, 0, 0};
        break;
    }
    return p;
  }

  void drawVisorScanlines() {
    for (int y = 36; y < 100; y += 3) {
      _canvas.drawFastHLine(40, y, 240, SCANLINE);
    }
  }

  void drawCapsule() {
    _canvas.fillRoundRect(36, 32, 248, 72, 36, VISOR);
    _canvas.fillRoundRect(44, 38, 232, 60, 30, VISOR_DEEP);
    drawVisorScanlines();
    // Soft outer glow (layered frames — pinu-bot / CRT-style displays)
    _canvas.drawRoundRect(21, 17, 278, 102, 50, INK_GLOW);
    _canvas.drawRoundRect(23, 19, 274, 98, 49, INK_GLOW);
    _canvas.drawRoundRect(24, 20, 272, 96, 48, INK);
    _canvas.drawLine(32, 24, 72, 24, INK_BRIGHT);  // glass highlight
  }

  void drawBrows(const Preset &p, float yOff, float tilt) {
    struct Pair { int a, b; };
    const Pair pairs[2] = {{62, 136}, {184, 258}};
    for (const auto &pr : pairs) {
      int a = pr.a, b = pr.b;
      int y0 = 46 + (int)yOff, y1 = 46 + (int)yOff;
      switch (p.brow) {
        case Brow::Angry:
          _canvas.drawLine(a, 48 + (int)yOff, b, (b < 200 ? 58 : 48) + (int)yOff + (int)tilt, INK);
          continue;
        case Brow::Sad:
          _canvas.drawLine(a, 58 + (int)yOff, b, (b < 200 ? 46 : 58) + (int)yOff + (int)tilt, INK);
          continue;
        case Brow::Raise:
          _canvas.drawLine(a - 4, 34 + (int)yOff, b + 4, 34 + (int)yOff + (int)tilt, INK);
          continue;
        case Brow::Arch: {
          int cx = (a + b) / 2;
          for (int i = 0; i < 8; i++) {
            float t0 = i / 8.0f, t1 = (i + 1) / 8.0f;
            int x0 = a + (int)((b - a) * t0), x1 = a + (int)((b - a) * t1);
            int ya = y0 + (int)(-(1 - (2 * fabsf(t0 - 0.5f))) * 8);
            int yb = y0 + (int)(-(1 - (2 * fabsf(t1 - 0.5f))) * 8);
            _canvas.drawLine(x0, ya, x1, yb + (int)tilt, INK);
          }
          continue;
        }
        case Brow::Asym:
          if (a < 150) _canvas.drawLine(a, 48 + (int)yOff, b, 40 + (int)yOff + (int)tilt, INK);
          else _canvas.drawLine(a, 58 + (int)yOff, b, 50 + (int)yOff - (int)tilt, INK);
          continue;
        default:
          _canvas.drawLine(a, y0, b, y1 + (int)tilt, INK);
      }
    }
  }

  void drawLidMask(int cx, int cy, int r, float top, float bot, LidAng ang) {
    if (ang == LidAng::AngryIn) {
      bool inner = cx > CXC;
      if (!inner) _canvas.fillTriangle(cx - r, cy - r, cx + r + 2, cy - r, cx + r + 2, cy - r + (int)(r * 0.55f), VISOR);
      else _canvas.fillTriangle(cx + r, cy - r, cx - r - 2, cy - r, cx - r - 2, cy - r + (int)(r * 0.55f), VISOR);
    } else if (ang == LidAng::SadOut) {
      bool outer = cx < CXC;
      if (outer) _canvas.fillTriangle(cx - r, cy + r, cx + r, cy + r, cx + r, cy + r - (int)(r * 0.5f), VISOR);
      else _canvas.fillTriangle(cx + r, cy + r, cx - r, cy + r, cx - r, cy + r - (int)(r * 0.5f), VISOR);
    }
    if (top > 0) _canvas.fillRect(cx - r - 1, cy - r - 1, r * 2 + 2, (int)(r * 2 * top) + 1, VISOR);
    if (bot > 0) {
      int h = (int)(r * 2 * bot);
      _canvas.fillRect(cx - r - 1, cy + r - h, r * 2 + 2, h + 1, VISOR);
    }
  }

  void drawWinkLine(int cx, int cy, int r) {
    for (int i = -2; i <= 2; i++) {
      _canvas.drawLine(cx - r + 8, cy + 2 + i, cx, cy + (int)(r * 0.35f) + i, PUP);
      _canvas.drawLine(cx, cy + (int)(r * 0.35f) + i, cx + r - 8, cy + 2 + i, PUP);
    }
  }

  void drawSquarePupil(int px, int py, int s) {
    _canvas.fillCircle(px, py, s, PUP);
    if (s >= 3) _canvas.fillCircle(px - s / 3, py - s / 3, s / 3, HILITE);
  }

  // Bender-style LED segment mouth (glow bars, not wireframe grid).
  void drawLedMouth(bool animate) {
    const int x0 = 78, x1 = 242, y0 = 138, y1 = 172;
    const int segs = 7, gap = 3, pad = 5;
    _canvas.fillRoundRect(x0, y0, x1 - x0, y1 - y0, 10, VISOR_DEEP);
    _canvas.drawRoundRect(x0, y0, x1 - x0, y1 - y0, 10, INK);
    const int troughH = y1 - y0 - pad * 2;
    const int innerW = x1 - x0 - pad * 2;
    const int segW = (innerW - gap * (segs - 1)) / segs;
    int sx = x0 + pad;
    float amp = animate ? (_mouthAmpSmooth / 100.0f) : 0.0f;
    for (int i = 0; i < segs; i++) {
      float u = (float)i / (segs - 1);
      float env = sinf(u * 3.14159265f);
      env = env * env;
      int sh = troughH;
      if (animate) {
        sh = (int)(troughH * (0.35f + 0.65f * amp * env));
        if (sh < 5) sh = 5;
      } else {
        sh = (int)(troughH * (0.55f + 0.45f * env));
      }
      int sy = y0 + pad + (troughH - sh) / 2;
      _canvas.fillRoundRect(sx - 1, sy - 1, segW + 2, sh + 2, 4, INK_GLOW);
      _canvas.fillRoundRect(sx, sy, segW, sh, 3, INK);
      if (!animate && i % 2 == 0) {
        _canvas.drawFastHLine(sx + 1, sy + 1, segW - 2, INK_BRIGHT);
      }
      sx += segW + gap;
    }
  }

  void drawHeartPupil(int px, int py, int s) {
    int hs = (int)(s * 1.08f);
    _canvas.fillCircle(px - hs / 3, py - hs / 6, hs / 2 + 1, RED);
    _canvas.fillCircle(px + hs / 3, py - hs / 6, hs / 2 + 1, RED);
    _canvas.fillTriangle(px - hs, py, px + hs, py, px, py + hs, RED);
  }

  void drawSpiralPupil(int cx, int cy) {
    uint32_t t = millis();
    int px0 = cx, py0 = cy;
    for (int i = 1; i <= 40; i++) {
      float a = i / 40.0f * 2.2f * 2 * PI + t * 0.004f;
      int rad = 2 + (int)(i * 0.35f);
      int px = cx + (int)(cosf(a) * rad);
      int py = cy + (int)(sinf(a) * rad);
      _canvas.drawLine(px0, py0, px, py, PURPLE);
      px0 = px; py0 = py;
    }
  }

  void drawXPupil(int px, int py) {
    for (int i = -1; i <= 1; i++) {
      _canvas.drawLine(px - 10, py - 10 + i, px + 10, py + 10 + i, PUP);
      _canvas.drawLine(px - 10, py + 10 + i, px + 10, py - 10 + i, PUP);
    }
  }

  void drawTear(int cx, int cy, int r) {
    int tx = cx - 6, ty = cy + r + 4;
    _canvas.fillCircle(tx, ty + 22, 8, BLUE);
    _canvas.fillTriangle(tx, ty - 14, tx - 10, ty + 4, tx + 10, ty + 4, BLUE);
    _canvas.fillTriangle(tx, ty - 14, tx - 10, ty + 10, tx + 10, ty + 10, BLUE);
    _canvas.fillCircle(tx - 3, ty + 6, 3, HILITE);
  }

  float mouthOffset(float u, float c, const MouthCfg &m, int x) const {
    float d = m.bow * c + m.tilt * (u - 0.5f);
    if (m.glitch) d += m.glitch * sinf(x * 0.35f) * (u > 0.3f && u < 0.7f ? 1.0f : 0.4f);
    return d;
  }

  float mouthTalkEnvelope(float u) const {
    float s = sinf(u * 3.14159265f);
    return s * s * s + 0.1f * s * s;
  }

  void mouthShapeAt(int x, const MouthCfg &m, int my, int w, int &outTop, int &outBot) const {
    float u = (float)(x - m.x0) / w;
    float c = 1.0f - (2 * u - 1) * (2 * u - 1);
    float edge = sqrtf(c < 0 ? 0 : c);
    float half = m.gap * MOUTH_H_MUL * (0.5f + 0.5f * edge) * (1.0f - m.pinch * c);
    int dy = (int)mouthOffset(u, c, m, x);
    outTop = my - (int)half + dy;
    outBot = my + (int)half + dy;
  }

  struct MouthPt { int x, y; };

  // Boca neutral editada en gestures-preview.html (JSON v3, mirror L+R + inferior).
  struct MouthNorm {
    float u, t;
  };
  static constexpr MouthNorm kMouthA = {0.5f, 0.1079f};
  static constexpr MouthNorm kMouthC = {0.0f, 0.4249f};
  static constexpr MouthNorm kMouthF = {0.1795f, 0.4268f};
  static constexpr MouthNorm kMouthH = {0.5f, 0.2259f};
  static constexpr MouthNorm kMouthJ = {0.0f, 0.3585f};
  static constexpr MouthNorm kMouthL = {0.4583f, 0.5f};
  static constexpr float kMouthVertU = 0.4583f;

  void mouthPtFromNorm(const MouthCfg &m, int my, MouthNorm n, MouthPt &out) const {
    const int w = m.x1 - m.x0;
    const int x = m.x0 + (int)(n.u * w + 0.5f);
    int topY, botY;
    mouthShapeAt(x, m, my, w, topY, botY);
    out.x = x;
    out.y = topY + (int)((botY - topY) * n.t + 0.5f);
  }

  int mouthMidYAt(int x, const MouthCfg &m, int my, int w) const {
    int topY, botY;
    mouthShapeAt(x, m, my, w, topY, botY);
    return (topY + botY) / 2;
  }

  int mirrorBelowY(int y, int x, const MouthCfg &m, int my, int w) const {
    int mid = mouthMidYAt(x, m, my, w);
    return mid + (mid - y);
  }

  static constexpr float kMouthTalkNeutral = 0.34f;
  static constexpr float kMouthTalkAmpCenter = 0.38f;
  static constexpr float kMouthTalkAmpScale = 1.62f;
  static constexpr float kMouthTalkOpenMax = 13.0f;
  static constexpr float kMouthTalkCloseMax = 8.0f;
  static constexpr float kMouthTalkSpreadGain = 0.92f;

  float mouthOpenFactor() const {
    if (_singing) return 0.72f + 0.28f * sinf(millis() * 0.05f);
    if (!_talking) return kMouthTalkNeutral;
    float amp = _mouthAmpSmooth / 100.0f;
    float open = kMouthTalkNeutral + (amp - kMouthTalkAmpCenter) * kMouthTalkAmpScale;
    if (open < 0.0f) open = 0.0f;
    if (open > 1.0f) open = 1.0f;
    return open;
  }

  float mouthSpreadDelta(float openFactor) const {
    float delta = openFactor - kMouthTalkNeutral;
    float gain = (delta >= 0.0f) ? kMouthTalkOpenMax : kMouthTalkCloseMax;
    return delta * gain * kMouthTalkSpreadGain;
  }

  int clampInnerLineY(int y, int x, bool isUpper, const MouthCfg &m, int my, int w) const {
    int topY, botY;
    mouthShapeAt(x, m, my, w, topY, botY);
    int mid = (topY + botY) / 2;
    const int margin = 5;
    if (isUpper) {
      if (y < topY + margin) y = topY + margin;
      if (y > mid - 2) y = mid - 2;
    } else {
      if (y > botY - margin) y = botY - margin;
      if (y < mid + 2) y = mid + 2;
    }
    return y;
  }

  int talkSpreadY(int x, int y, bool isUpper, const MouthCfg &m, int my, int w, float openFactor) const {
    float u = (float)(x - m.x0) / (float)w;
    float d = mouthSpreadDelta(openFactor) * sinf(u * 3.14159265f);
    int ny = isUpper ? (int)(y - d + 0.5f) : (int)(y + d + 0.5f);
    return clampInnerLineY(ny, x, isUpper, m, my, w);
  }

  static MouthPt catmullPt(const MouthPt &p0, const MouthPt &p1, const MouthPt &p2, const MouthPt &p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    MouthPt q;
    q.x = (int)(0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t +
      (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
      (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3) + 0.5f);
    q.y = (int)(0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t +
      (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
      (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3) + 0.5f);
    return q;
  }

  void drawSpline(const MouthPt *pts, int n, int steps = 8) {
    if (n < 2) return;
    int x0 = pts[0].x, y0 = pts[0].y;
    for (int i = 0; i < n - 1; i++) {
      MouthPt p0 = pts[i > 0 ? i - 1 : 0];
      MouthPt p1 = pts[i];
      MouthPt p2 = pts[i + 1];
      MouthPt p3 = pts[i + 2 < n ? i + 2 : n - 1];
      for (int s = 1; s <= steps; s++) {
        MouthPt q = catmullPt(p0, p1, p2, p3, s / (float)steps);
        _canvas.drawLine(x0, y0, q.x, q.y, INK);
        x0 = q.x;
        y0 = q.y;
      }
    }
  }

  void drawSideBulb(int xEdge, int yLine, int anchorY, bool isUpper, bool isLeft) {
    int cx = isLeft ? xEdge + 4 : xEdge - 4;
    int cy = isUpper ? anchorY + 2 : anchorY - 2;
    int xStart = isLeft ? xEdge + 4 : xEdge - 4;
    int px0 = xStart, py0 = yLine;
    const int segs = 6;
    for (int s = 1; s <= segs; s++) {
      float t = s / (float)segs;
      float u = 1.0f - t;
      int px = (int)(u * u * xStart + 2 * u * t * cx + t * t * xEdge + 0.5f);
      int py = (int)(u * u * yLine + 2 * u * t * cy + t * t * anchorY + 0.5f);
      _canvas.drawLine(px0, py0, px, py, INK);
      px0 = px;
      py0 = py;
    }
  }

  void drawMouthGridCustom(const MouthCfg &m) {
    const int my = 154;
    const int w = m.x1 - m.x0;
    const float openFactor = mouthOpenFactor();

    MouthPt F, H, J, L;
    mouthPtFromNorm(m, my, kMouthF, F);
    mouthPtFromNorm(m, my, kMouthH, H);
    mouthPtFromNorm(m, my, kMouthJ, J);
    mouthPtFromNorm(m, my, kMouthL, L);

    MouthPt K = {m.x1, J.y};
    MouthPt G = {m.x0 + (int)((1.0f - kMouthF.u) * w + 0.5f), F.y};
    MouthPt Lr = {m.x0 + (int)((1.0f - kMouthVertU) * w + 0.5f), L.y};

    int shapeLtop, shapeLbot, shapeRtop, shapeRbot;
    mouthShapeAt(m.x0, m, my, w, shapeLtop, shapeLbot);
    mouthShapeAt(m.x1, m, my, w, shapeRtop, shapeRbot);

    _canvas.drawLine(m.x0, shapeLtop, m.x0, shapeLbot, INK);
    _canvas.drawLine(m.x1, shapeRtop, m.x1, shapeRbot, INK);
    int pT = shapeLtop, pB = shapeLbot;
    for (int x = m.x0 + 1; x <= m.x1; x++) {
      int topY, botY;
      mouthShapeAt(x, m, my, w, topY, botY);
      _canvas.drawLine(x - 1, pT, x, topY, INK);
      _canvas.drawLine(x - 1, pB, x, botY, INK);
      pT = topY;
      pB = botY;
    }

    const int xVertL = L.x;
    const int xVertR = Lr.x;
    for (int i = 1; i < m.teeth; i++) {
      int x = m.x0 + (w * i) / m.teeth;
      if (abs(x - xVertL) < 3 || abs(x - xVertR) < 3) continue;
      int topY, botY;
      mouthShapeAt(x, m, my, w, topY, botY);
      _canvas.drawLine(x, topY, x, botY, INK);
    }
    for (int vx : {xVertL, xVertR}) {
      int topY, botY;
      mouthShapeAt(vx, m, my, w, topY, botY);
      _canvas.drawLine(vx, topY, vx, botY, INK);
    }

    const int xIL = m.x0 + 5;
    const int xIR = m.x1 - 5;
    int yJl = talkSpreadY(xIL, J.y, true, m, my, w, openFactor);
    int yKr = talkSpreadY(xIR, K.y, true, m, my, w, openFactor);
    int yFl = talkSpreadY(F.x, F.y, true, m, my, w, openFactor);
    int yHl = talkSpreadY(H.x, H.y, true, m, my, w, openFactor);
    int yGr = talkSpreadY(G.x, G.y, true, m, my, w, openFactor);
    int yIl = mirrorBelowY(yHl, H.x, m, my, w);

    MouthPt upper[5] = {
      {xIL, yJl},
      {F.x, yFl},
      {H.x, yHl},
      {G.x, yGr},
      {xIR, yKr},
    };
    MouthPt lower[5] = {
      {xIL, talkSpreadY(xIL, mirrorBelowY(yJl, xIL, m, my, w), false, m, my, w, openFactor)},
      {F.x, talkSpreadY(F.x, mirrorBelowY(yFl, F.x, m, my, w), false, m, my, w, openFactor)},
      {H.x, talkSpreadY(H.x, yIl, false, m, my, w, openFactor)},
      {G.x, talkSpreadY(G.x, mirrorBelowY(yGr, G.x, m, my, w), false, m, my, w, openFactor)},
      {xIR, talkSpreadY(xIR, mirrorBelowY(yKr, xIR, m, my, w), false, m, my, w, openFactor)},
    };

    drawSpline(upper, 5, 8);
    drawSpline(lower, 5, 8);

    int anchorTopL = shapeLtop + 6;
    int anchorBotL = shapeLbot - 6;
    int anchorTopR = shapeRtop + 6;
    int anchorBotR = shapeRbot - 6;
    drawSideBulb(m.x0, upper[0].y, anchorTopL, true, true);
    drawSideBulb(m.x0, lower[0].y, anchorBotL, false, true);
    drawSideBulb(m.x1, upper[4].y, anchorTopR, true, false);
    drawSideBulb(m.x1, lower[4].y, anchorBotR, false, false);
  }

  int mouthRowYAt(int x, const MouthCfg &m, int my, int w, int rowDiv, int rows,
                  float talkOpen, bool animMouth) const {
    int topY, botY;
    mouthShapeAt(x, m, my, w, topY, botY);
    float u = (float)(x - m.x0) / w;
    float env = animMouth ? mouthTalkEnvelope(u) : 0;
    int openDy = (int)lroundf(talkOpen * env);
    float rowFrac = (float)rowDiv / rows;
    int baseH = topY + (botY - topY) * rowDiv / rows;
    int rowShift = (rowFrac < 0.5f) ? -openDy : openDy;
    return baseH + rowShift;
  }

  void drawMouthGridProcedural(const MouthCfg &m) {
    const int my = 154;
    const int w = m.x1 - m.x0;
    float talkOpen = 0;
    if (_singing) {
      talkOpen = 3.0f + 2.0f * sinf(millis() * 0.05f);
    } else if (_talking) {
      float a = _mouthAmpSmooth / 100.0f;
      a = a * (2.0f - a);
      talkOpen = 0.2f + a * 3.2f;
    }
    const bool animMouth = _talking || _singing;
    static const int kInnerRows = 2;
    static const int kInnerDiv[2] = {1, 2};
    const int gridRows = 3;

    int pT = 0, pB = 0;
    for (int x = m.x0; x <= m.x1; x++) {
      int topY, botY;
      mouthShapeAt(x, m, my, w, topY, botY);
      if (x == m.x0 || x == m.x1) {
        _canvas.drawLine(x, topY, x, botY, INK);
      } else {
        _canvas.drawLine(x - 1, pT, x, topY, INK);
        _canvas.drawLine(x - 1, pB, x, botY, INK);
      }
      pT = topY;
      pB = botY;
    }

    for (int i = 1; i < m.teeth; i++) {
      int x = m.x0 + (w * i) / m.teeth;
      int topY, botY;
      mouthShapeAt(x, m, my, w, topY, botY);
      _canvas.drawLine(x, topY, x, botY, INK);
    }

    for (int ri = 0; ri < kInnerRows; ri++) {
      int lx = m.x0;
      int ly = mouthRowYAt(lx, m, my, w, kInnerDiv[ri], gridRows, talkOpen, animMouth);
      for (int x = m.x0 + 1; x <= m.x1; x++) {
        int hy = mouthRowYAt(x, m, my, w, kInnerDiv[ri], gridRows, talkOpen, animMouth);
        _canvas.drawLine(lx, ly, x, hy, INK);
        lx = x;
        ly = hy;
      }
    }
  }

  void drawMouthGrid(const MouthCfg &m) {
    if (_emotion == Emotion::Neutral) {
      drawLedMouth(_talking || _singing);
      return;
    }
    drawMouthGridProcedural(m);
  }

  void drawCompactMouth() {
    const int cx = 160, cy = 154, hw = 28, hh = 17;
    _canvas.drawRoundRect(cx - hw, cy - hh, hw * 2, hh * 2, 6, INK);
    for (int row = 1; row < 3; row++) {
      int y = cy - hh + (hh * 2 * row) / 3;
      _canvas.drawLine(cx - hw + 4, y, cx + hw - 4, y, INK);
    }
    for (int col = 1; col < 4; col++) {
      int x = cx - hw + (hw * 2 * col) / 4;
      _canvas.drawLine(x, cy - hh + 4, x, cy + hh - 4, INK);
    }
  }

  void drawOMouth() {
    _canvas.drawCircle(CXC, 154, 15, INK);
    _canvas.drawCircle(CXC, 154, 14, INK);
  }

  // Drawn straight onto the screen (not the shifted sprite) so it stays pinned
  // to the top-right corner regardless of FACE_OFFSET_X.
  void drawGearOnScreen() {
    const int cx = (_faceScreenW <= 250) ? 227 : 303;
    const int cy = 16, r = 9;
    static const int8_t dx[8] = {11, 8, 0, -8, -11, -8, 0, 8};
    static const int8_t dy[8] = {0, 8, 11, 8, 0, -8, -11, -8};
    for (int i = 0; i < 8; i++) _gfx.fillRect(cx + dx[i] - 3, cy + dy[i] - 3, 6, 6, GEAR);
    _gfx.fillCircle(cx, cy, r, GEAR);
    _gfx.fillCircle(cx, cy, 4, BG);
  }

  void drawEyeSide(bool left, const Preset &base, float blink) {
    int cx = left ? EYE_L : EYE_R;
    int cy = EYE_Y;
    int r = base.eyeR;
    bool wink = (left && base.winkLeft) || (!left && base.winkRight);
    uint16_t tint = base.eyeTint ? base.eyeTint : EYE;

    if (wink) {
      _canvas.fillCircle(cx, cy, r + 4, EYE_GLOW);
      _canvas.fillCircle(cx, cy, r, tint);
      drawWinkLine(cx, cy, r);
      return;
    }

    _canvas.fillCircle(cx, cy, r + 5, EYE_GLOW);
    _canvas.fillCircle(cx, cy, r + 2, 0x52AA);
    _canvas.fillCircle(cx, cy, r, tint);

    float top = base.topLid, bot = base.botLid;
    if (left && base.topLidL >= 0) top = base.topLidL;
    if (!left && base.topLidR >= 0) top = base.topLidR;
    if (left && base.botLidL >= 0) bot = base.botLidL;
    if (!left && base.botLidR >= 0) bot = base.botLidR;

    if (!wink) {
      top = fmaxf(top, blink * 0.9f);
      bot = fmaxf(bot, blink * 0.9f);
    }
    if (_bored) top = fmaxf(top, 0.45f);

    drawLidMask(cx, cy, r, top, bot, base.lidAng);

    if (base.pupil == Pupil::Spiral) { drawSpiralPupil(cx, cy); return; }
    if (base.pupil == Pupil::X) { drawXPupil(cx, cy); return; }

    int px = cx + (int)_gazeX + base.pupilOffX;
    int py = cy + (int)_gazeY + base.pupilOffY;
    if (base.pupil == Pupil::Heart) drawHeartPupil(px, py, base.pupilS);
    else drawSquarePupil(px, py, base.pupilS);
  }

  void draw() {
    Preset p = preset();
    _canvas.fillSprite(BG);

    float t = millis() * 0.001f;
    float browDy = sinf(t * 4.0f + _browPhase) * 0.8f;
    float browTilt = sinf(t * 3.0f + _browPhase * 0.7f) * 0.6f;

    drawCapsule();
    drawBrows(p, browDy, browTilt);

    float blink = _blinkAmt;
    if (p.winkRight) {
      drawEyeSide(true, p, blink);
      drawEyeSide(false, p, 0);
    } else if (p.winkLeft) {
      drawEyeSide(true, p, 0);
      drawEyeSide(false, p, blink);
    } else {
      drawEyeSide(true, p, blink);
      drawEyeSide(false, p, blink);
    }

    if (p.tear) drawTear(EYE_L, EYE_Y, p.eyeR);

    if (p.mouthKind == MouthKind::Compact) drawCompactMouth();
    else if (p.mouthKind == MouthKind::O) drawOMouth();
    else drawMouthGrid(p.mouth);

    if (_faceScreenW < FACE_DESIGN_W) {
      const float zoom = (float)_faceScreenW / (float)FACE_DESIGN_W;
      _canvas.pushRotateZoom(_faceScreenW * 0.5f + FACE_OFFSET_X, FACE_H * 0.5f + FACE_OFFSET_Y, 0.0f, zoom, zoom);
    } else {
      _canvas.pushSprite(FACE_OFFSET_X, FACE_OFFSET_Y);
    }
    if (_showGear) drawGearOnScreen();
  }
};

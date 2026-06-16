// Bender face renderer (LovyanGFX) — faithful to the reference sheet:
// WHITE background, BLACK line-art. The visor is a black-filled capsule with
// concentric black rim rings; the eyes are WHITE holes (the background showing
// through) with black square pupils, reshaped per emotion. The mouth is a black
// outlined teeth grid (white teeth = background) that bows/pinches per emotion
// and ripples with a sine wave while speaking. SURPRISED keeps the capsule visor + "O".
//
// Double-buffered in a PSRAM sprite (no flicker), repainted only on change.
// Landscape (rotation 1) chosen on purpose: the wide visor needs the width.
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
    _gfx.fillScreen(TFT_BLACK);   // text strip below the face
  }

  void setEmotion(Emotion e) { if (e != _emotion) { _emotion = e; _dirty = true; } }

  // Show the settings gear in the top-right corner (tappable while idle).
  void setShowGear(bool v) { if (v != _showGear) { _showGear = v; _dirty = true; } }
  // Force a full repaint, e.g. after the settings menu painted over the screen.
  void redraw() { _dirty = true; }
  // Screen-space hit test for the gear (sprite is pushed at 0,0).
  static bool gearHit(int sx, int sy) { return sx >= 282 && sy <= 40; }

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

  // Status text lives in a dark strip below the white face (keeps colour cues readable).
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
  bool _dirty = true, _blinking = false, _talking = false, _showGear = false;
  uint8_t _mouthAmp = 0;
  uint32_t _blinkUntil = 0, _nextBlinkAt = 3000;

  static constexpr int FACE_H = 200;
  static constexpr uint16_t BG = 0xFFFF;   // white background
  static constexpr uint16_t INK = 0x0000;  // black line-art (visor body, pupils, grid)
  // Colour accents — the face stays line-art, colour only marks an emotion at a glance.
  static constexpr uint16_t RED = 0xF800;  // love: heart eyes
  static constexpr uint16_t BLUE = 0x041F; // sad: tear
  static constexpr uint16_t GEAR = 0x5C9F; // settings gear icon (blue, "tap me")
  static constexpr int CXC = 160;

  // Black capsule visor with concentric rim rings (white gaps = background).
  void drawCapsule(int x, int y, int w, int h) {
    int r = h / 2;
    _canvas.fillRoundRect(x + 9, y + 9, w - 18, h - 18, r - 9, INK);  // black interior
    _canvas.drawRoundRect(x + 5, y + 5, w - 10, h - 10, r - 5, INK);  // inner ring
    _canvas.drawRoundRect(x, y, w, h, r, INK);                        // outer ring
  }

  void drawSquarePupil(int px, int py, int s = 9) { _canvas.fillRect(px - s, py - s, s * 2, s * 2, INK); }

  // Love: a small red heart instead of the square pupil (two lobes + a point).
  void drawHeartPupil(int px, int py) {
    _canvas.fillCircle(px - 5, py - 3, 6, RED);
    _canvas.fillCircle(px + 5, py - 3, 6, RED);
    _canvas.fillTriangle(px - 11, py - 1, px + 11, py - 1, px, py + 12, RED);
  }

  // Dizzy: knocked-out "X" eyes (thick diagonal cross) instead of a pupil.
  void drawXPupil(int px, int py) {
    for (int i = -1; i <= 1; i++) {
      _canvas.drawLine(px - 9, py - 9 + i, px + 9, py + 9 + i, INK);
      _canvas.drawLine(px - 9, py + 9 + i, px + 9, py - 9 + i, INK);
    }
  }

  // WHITE eye hole inside the black visor, reshaped per emotion; the pupil follows the
  // visible white band so the black square never spills into the visor. Distinct shape
  // for each of the 12 emotions; colour only on love (heart) and is added in drawAccents.
  void drawEye(int cx, int cy, bool isLeft) {
    const int ew = 110, eh = 68;
    const int x = cx - ew / 2, y = cy - eh / 2;

    if (_blinking) { _canvas.fillRoundRect(x, cy - 5, ew, 10, 5, BG); return; }

    if (_emotion == Emotion::Cool && !isLeft) {     // wink: shallow curved white line
      for (int i = -1; i <= 1; i++) {
        _canvas.drawLine(x + 14, cy + 2 + i, cx, cy + 7 + i, BG);
        _canvas.drawLine(cx, cy + 7 + i, x + ew - 14, cy + 2 + i, BG);
      }
      return;
    }

    _canvas.fillRoundRect(x, y, ew, eh, 16, BG);

    const int cut = (int)(eh * 0.42f);   // how deep an angled lid bites in
    int pupilY = cy, pupilX = cx;

    switch (_emotion) {
      case Emotion::Angry: {               // both brows furrow down toward the centre
        int inY = y + cut;
        if (isLeft) _canvas.fillTriangle(x, y - 2, x + ew + 2, y - 2, x + ew + 2, inY, INK);
        else        _canvas.fillTriangle(x + ew, y - 2, x - 2, y - 2, x - 2, inY, INK);
        pupilY = cy + cut / 4;
        break;
      }
      case Emotion::Confused: {            // asymmetric: left brow furrows, right rises
        if (isLeft) {
          _canvas.fillTriangle(x, y - 2, x + ew + 2, y - 2, x + ew + 2, y + cut, INK);
          pupilY = cy + cut / 4;
        } else {
          _canvas.fillRect(x, y, ew, (int)(eh * 0.22f), INK);  // slight raised top lid
          pupilY = cy - 5;
        }
        break;
      }
      case Emotion::Sad: {                 // inner-bottom lid up (droop), outer stays low
        int inY = y + eh - cut;
        if (isLeft) _canvas.fillTriangle(x, y + eh + 2, x + ew + 2, y + eh + 2, x + ew + 2, inY, INK);
        else        _canvas.fillTriangle(x + ew, y + eh + 2, x - 2, y + eh + 2, x - 2, inY, INK);
        pupilY = cy - cut / 4;
        break;
      }
      case Emotion::Happy:                 // squint smile: lower lid up
      case Emotion::Love: {                // love uses the same squint + heart pupil
        int lid = (int)(eh * 0.26f);
        _canvas.fillRect(x, y + eh - lid, ew, lid + 1, INK);
        pupilY = cy - lid / 2;
        break;
      }
      case Emotion::Excited:               // wide eyes (fully open) — read together with
        pupilY = cy;                       // the big grin; bigger pupil below
        break;
      case Emotion::Sleepy: {              // heavy upper lid, half closed
        int lid = (int)(eh * 0.50f);
        _canvas.fillRect(x, y, ew, lid, INK);
        pupilY = y + lid + (eh - lid) / 2;
        break;
      }
      case Emotion::Thinking:              // glance up-and-away (pondering)
        pupilY = cy - 9;
        pupilX = cx + 12;
        break;
      case Emotion::Dizzy:                 // X eyes, no square pupil
        drawXPupil(cx, cy);
        return;
      default: break;                      // Neutral, Surprised, Cool(open eye)
    }

    if (_emotion == Emotion::Love)         drawHeartPupil(pupilX, pupilY);
    else if (_emotion == Emotion::Excited) drawSquarePupil(pupilX, pupilY, 11);  // big-eyed
    else                                   drawSquarePupil(pupilX, pupilY);
  }

  // Vertical offset of the mouth outline at column position t (0..1), per emotion.
  float mouthOffset(float t, float c, float bow, float tilt, bool dz, float wave, float phase, int x) {
    float d = bow * c + tilt * (t - 0.5f) + wave * sinf(x * 0.20f + phase);
    if (dz) d += 4.0f * sinf(x * 0.15f + phase * 0.5f);  // woozy ripple at rest
    return d;
  }

  // Black teeth lattice on white: rounded outline + 2 interior rows + vertical dividers.
  // Bows into a smile/frown, pinches when gritted, tilts for cool/confused, ripples with
  // audio while speaking. White "teeth" = background between the black lines.
  void drawTeethMouth() {
    const int mx0 = 100, mx1 = 220, WD = mx1 - mx0;
    const int myc = 158, gap = 17, teeth = 6;

    float phase = millis() / 80.0f;
    float wave = _talking ? (_mouthAmp / 100.0f) * 8.0f : 0.0f;
    float bow = 0.0f, pinch = 0.0f, tilt = 0.0f;
    bool dz = false;
    if (!_talking) {
      switch (_emotion) {
        case Emotion::Happy: case Emotion::Love: bow = -7.0f; break;
        case Emotion::Excited:                   bow = -10.0f; break;
        case Emotion::Sad:                       bow = 7.0f; break;
        case Emotion::Sleepy:                    bow = 4.0f; break;
        case Emotion::Angry:                     pinch = 0.5f; break;
        case Emotion::Confused:                  pinch = 0.3f; tilt = 9.0f; break;
        case Emotion::Cool:                      tilt = 7.0f; break;   // smirk
        case Emotion::Dizzy:                     dz = true; break;
        default: break;
      }
    }

    int pT = 0, pB = 0, pH1 = 0, pH2 = 0;
    for (int x = mx0; x <= mx1; x++) {
      float t = (float)(x - mx0) / WD;
      float c = 1.0f - (2 * t - 1) * (2 * t - 1);
      float edge = sqrtf(c < 0 ? 0 : c);
      float half = gap * (0.5f + 0.5f * edge) * (1.0f - pinch * c);
      int dy = (int)mouthOffset(t, c, bow, tilt, dz, wave, phase, x);
      int topY = myc - (int)half + dy, botY = myc + (int)half + dy;
      int h1 = topY + (botY - topY) / 3, h2 = topY + 2 * (botY - topY) / 3;
      if (x == mx0 || x == mx1) {
        _canvas.drawLine(x, topY, x, botY, INK);          // rounded end caps
      } else {
        _canvas.drawLine(x - 1, pT, x, topY, INK);
        _canvas.drawLine(x - 1, pT + 1, x, topY + 1, INK);
        _canvas.drawLine(x - 1, pB, x, botY, INK);
        _canvas.drawLine(x - 1, pB - 1, x, botY - 1, INK);
        _canvas.drawLine(x - 1, pH1, x, h1, INK);         // upper interior row
        _canvas.drawLine(x - 1, pH2, x, h2, INK);         // lower interior row
      }
      pT = topY; pB = botY; pH1 = h1; pH2 = h2;
    }
    for (int i = 1; i < teeth; i++) {     // vertical tooth dividers (ride the wave)
      int x = mx0 + (WD * i) / teeth;
      float t = (float)(x - mx0) / WD, c = 1.0f - (2 * t - 1) * (2 * t - 1);
      float half = gap * (0.5f + 0.5f * sqrtf(c < 0 ? 0 : c)) * (1.0f - pinch * c);
      int dy = (int)mouthOffset(t, c, bow, tilt, dz, wave, phase, x);
      _canvas.drawLine(x, myc - (int)half + dy, x, myc + (int)half + dy, INK);
    }
  }

  // Colour cues drawn on top — kept minimal so the face stays line-art.
  void drawAccents() {
    if (_talking) return;
    if (_emotion == Emotion::Sad) {       // a blue tear falling under the left eye
      int tx = CXC - 62, ty = 122;
      _canvas.fillTriangle(tx - 5, ty, tx + 5, ty, tx, ty - 10, BLUE);
      _canvas.fillCircle(tx, ty + 2, 5, BLUE);
    }
  }

  // Small open "O" mouth (line-art) — the surprised cue, same style as the rest.
  void drawOMouth() {
    _canvas.drawCircle(CXC, 152, 15, INK);
    _canvas.drawCircle(CXC, 152, 14, INK);
  }

  // Settings gear in the top-right corner (8 teeth + hub + hole).
  void drawGear() {
    const int cx = 303, cy = 16, r = 9;
    static const int8_t dx[8] = {11, 8, 0, -8, -11, -8, 0, 8};
    static const int8_t dy[8] = {0, 8, 11, 8, 0, -8, -11, -8};
    for (int i = 0; i < 8; i++) _canvas.fillRect(cx + dx[i] - 3, cy + dy[i] - 3, 6, 6, GEAR);
    _canvas.fillCircle(cx, cy, r, GEAR);
    _canvas.fillCircle(cx, cy, 4, BG);   // hub hole
  }

  void draw() {
    _canvas.fillSprite(BG);
    drawCapsule(22, 22, 276, 92);          // SURPRISED now uses the same capsule visor
    drawEye(CXC - 62, 66, true);
    drawEye(CXC + 62, 66, false);
    if (_emotion == Emotion::Surprised) drawOMouth();
    else drawTeethMouth();
    drawAccents();
    if (_showGear) drawGear();
    _canvas.pushSprite(0, 0);
  }
};

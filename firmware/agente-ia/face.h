// Capsule visor face (LovyanGFX). Dark CRT/LED aesthetic: glow, scanlines,
// lit eyes with catchlight pupils, Bender-style LED segment mouth at neutral.
#pragma once

#include <Arduino.h>
#include <math.h>
#include <LovyanGFX.hpp>
#include "config.h"
#include "playback_spectrum.h"
#include "speech_caption.h"

enum class Emotion {
  Neutral, Happy, Sad, Angry, Surprised, Thinking, Sleepy,
  Love, Excited, Cool, Confused, Dizzy, Vibing
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
  if (s == "vibing") return Emotion::Vibing;
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

  void setEmotion(Emotion e, float intensity = 1.0f) {
    _intensity = max(0.0f, min(1.0f, intensity));
    if (e != _emotion) {
      // brief squint so the new expression doesn't pop hard
      uint32_t tn = millis();
      _blinkEndAt    = tn + 250;
      _nextBlinkAt   = _blinkEndAt + 2000 + random(2000);
      _transitionEnd = tn + 420;
      _emotion = e;
      _gazeX = _gazeY = 0;
      _gazeTx = _gazeTy = 0;
      if (e == Emotion::Vibing) {
        _vibingBobY = 0;
        _lastVibingBobDraw = 0;
        _lastVibingDraw = 0;
        for (int i = 0; i < kVibingNotes; i++) _noteY[i] = -50.0f;
        _lastNoteSpawn = 0;
        _vibingLidPulse = 0.5f;
        _gazeX = _gazeY = 0;
        memset(_vibingBands, 0, sizeof(_vibingBands));
        memset(_colHist, 0, sizeof(_colHist));
      }
      _nextGazeAt = millis() + 700 + random(1800);
      _dirty = true;
    }
  }

  void setBored(bool b) { if (b != _bored) { _bored = b; _dirty = true; } }
  void setShowGear(bool v) { if (v != _showGear) { _showGear = v; _dirty = true; } }

  // Trigger a double blink (second blink ~250 ms after the first).
  void triggerDoubleBlink() {
    uint32_t now = millis();
    if (now >= _blinkEndAt) {
      _nextBlinkAt = now;
      _pendingExtraBlink = true;
    }
  }

  // Smoothly move gaze to (tx, ty) for durationMs, then resume random gaze.
  void setMicroGaze(int tx, int ty, uint32_t durationMs) {
    _gazeTx = (float)tx;
    _gazeTy = (float)ty;
    _nextGazeAt = millis() + durationMs + 300 + random(1000);
    _dirty = true;
  }

  // Título superpuesto SOBRE la cara (música/historia). Se dibuja DENTRO del sprite
  // (doble búfer) => transparente sobre el fondo, sin recuadro negro ni parpadeo.
  void setTopTitle(const String &t) {
    String safe;
    foldSpanishAscii(t, safe);
    if (safe.length() == 0) safe = t;
    if (safe == _topTitleRaw) return;
    _topTitleRaw = safe;
    // Precalcular layout UNA vez (recorte + X centrado). Hacerlo por-frame con textWidth()
    // robaba CPU al loop de audio y causaba underrun (golpe rítmico) en música/historia.
    _canvas.setFont(&fonts::DejaVu12);
    String line = safe;
    while (line.length() > 1 && _canvas.textWidth(line) > FACE_DESIGN_W - 12) {
      line = line.substring(0, line.length() - 1);
    }
    int w = _canvas.textWidth(line);
    _topTitle = line;
    _topTitleX = (FACE_DESIGN_W - w) / 2;
    if (_topTitleX < 2) _topTitleX = 2;
    _dirty = true;
  }
  void clearTopTitle() { if (_topTitle.length()) { _topTitle = ""; _topTitleRaw = ""; _dirty = true; } }
  Emotion emotion() const { return _emotion; }
  bool isVibing() const { return _emotion == Emotion::Vibing; }
  void redraw() { _dirty = true; }
  void resetAmpGain() { _ampEnvMax = 200.0f; }
  void setListening(bool v) { if (v != _listening) { _listening = v; _dirty = true; } }
  bool isListening() const { return _listening; }

  // Mic spectrum for vibing (bands 0..255, peak = raw sample peak).
  void setVibingMicGain(uint8_t pct) {
    if (pct < 50) pct = 50;
    if (pct > 300) pct = 300;
    if (pct == _vibingMicGain) return;
    _vibingMicGain = pct;
    _dirty = true;
  }
  uint8_t vibingMicGain() const { return _vibingMicGain; }

  void setVibingRange(uint16_t floor, uint16_t ceil) {
    if (floor > 500) floor = 500;
    if (ceil < 200) ceil = 200;
    if (ceil > 900) ceil = 900;
    if (floor >= ceil - 40) {
      floor = (ceil > 40) ? (uint16_t)(ceil - 40) : 0;
    }
    _vibingFloor = floor;
    _vibingCeil = ceil;
    _dirty = true;
  }
  uint16_t vibingFloor() const { return _vibingFloor; }
  uint16_t vibingCeil() const { return _vibingCeil; }

  float mapVibingLevel(float raw) const {
    if (raw <= (float)_vibingFloor) return 0.0f;
    if (raw >= (float)_vibingCeil) return 220.0f;
    return (raw - (float)_vibingFloor) / ((float)_vibingCeil - (float)_vibingFloor) * 220.0f;
  }

  void setVibingSpectrum(const uint8_t *bands, int n, uint32_t peak) {
    if (_emotion != Emotion::Vibing || !bands || n < 1) return;
    (void)peak;
    const int nb = n > kVibingBands ? kVibingBands : n;

    // AUTO-GAIN: adapt to the room instead of relying on a hand-tuned floor/ceil. Track a
    // decaying peak of the loudest band (fast attack, slow release) and normalise every band
    // against it, with an adaptive noise gate. Bars use the full height whatever the mic
    // level is — quiet rooms and loud rooms both react well with no manual calibration.
    const float gain = _vibingMicGain / 100.0f;        // mic sensitivity (lifts a weak mic)
    float rawB[kVibingBands];
    float frameMax = 0.0f;
    for (int i = 0; i < nb; i++) {
      rawB[i] = (float)bands[i] * gain;
      if (rawB[i] > frameMax) frameMax = rawB[i];
    }
    // Auto ceiling: jump to peaks, fall fast enough to keep following the beat (so the bars
    // visibly drop in the gaps). A small FIXED gate kills hiss; the gain decides how easily
    // the (weak) mic clears it.
    _vibAgcMax = fmaxf(frameMax, _vibAgcMax * 0.975f);
    if (_vibAgcMax < 30.0f) _vibAgcMax = 30.0f;
    const float gate = 14.0f;
    const float range = fmaxf(1.0f, _vibAgcMax - gate);

    for (int i = 0; i < nb; i++) {
      float nrm = (rawB[i] - gate) / range;
      if (nrm < 0.0f) nrm = 0.0f;
      if (nrm > 1.0f) nrm = 1.0f;
      float tgt = nrm * 220.0f;
      float cur = _vibingBands[i];
      const float rate = (tgt > cur) ? 0.60f : 0.42f;  // snappy attack so hits pop
      _vibingBands[i] = cur + (tgt - cur) * rate;
    }
    pushVibingHistoryColumn();
    pushVibingHistoryColumn();

    // Mouth amplitude from the same auto-gained overall level (gentle curve = real dynamics).
    float onrm = (frameMax - gate) / range;
    if (onrm < 0.0f) onrm = 0.0f;
    if (onrm > 1.0f) onrm = 1.0f;
    int lvl = (int)(powf(onrm, 0.62f) * 100.0f);
    if (lvl > 100) lvl = 100;
    setMouthAmplitude((uint8_t)lvl);
    _dirty = true;
  }

  // Waterfall espejo: desplaza historial y empuja snapshot de las bandas actuales.
  void pushVibingHistoryColumn() {
    float peak = 0.0f, sum = 0.0f;
    for (int b = 0; b < kVibingBands; b++) {
      sum += _vibingBands[b];
      if (_vibingBands[b] > peak) peak = _vibingBands[b];
    }
    const float avg = sum / (float)kVibingBands;
    const float mix = peak * 0.62f + avg * 0.38f;
    int lvl = (int)(mix + 0.5f);
    if (lvl > 220) lvl = 220;
    if (lvl < 0) lvl = 0;
    for (int x = 0; x < kSpecCols - 1; x++) {
      _colHist[x] = _colHist[x + 1];
    }
    _colHist[kSpecCols - 1] = (uint8_t)lvl;
  }

  // Nivel de columna cx: banda de frecuencia (izq=bajos) + rastro waterfall.
  uint8_t vibingColumnAt(int cx) const {
    const float u = (float)cx / (float)(kSpecCols - 1);
    const float bf = u * (float)(kVibingBands - 1);
    const int i0 = (int)bf;
    const int i1 = (i0 + 1 < kVibingBands) ? i0 + 1 : i0;
    const float frac = bf - (float)i0;
    const float bandV = _vibingBands[i0] * (1.0f - frac) + _vibingBands[i1] * frac;
    const float histV = (float)_colHist[cx];
    float v = fmaxf(bandV, histV * 0.82f);
    if (v > 220.0f) v = 220.0f;
    return (uint8_t)(v + 0.5f);
  }

  // Durante TTS/música el mic RX está apagado: animar boca desde PCM de reproducción.
  void feedPlaybackMouth(uint8_t level) {
    level = level > 100 ? 100 : level;
    setMouthAmplitude(level);
    if (_emotion != Emotion::Vibing) return;
    float t = millis() * 0.001f;
    float n = (float)level / 100.0f;
    n = fminf(1.0f, n * 1.55f);
    for (int i = 0; i < kVibingBands; i++) {
      float u = (float)i / (float)(kVibingBands - 1);
      float env = sinf(u * 3.14159265f);
      env = env * env;
      float wob = 0.72f + 0.28f * sinf(t * 16.0f + i * 0.9f);
      float tgt = 220.0f * n * env * wob;
      const float rate = (tgt > _vibingBands[i]) ? 0.78f : 0.35f;
      _vibingBands[i] += (tgt - _vibingBands[i]) * rate;
    }
    pushVibingHistoryColumn();
    _dirty = true;
  }

  // Goertzel por banda (bajos/medios/agudos) + beat en graves; sincronizado con I2S.
  void feedPlaybackPcm(const int16_t *samples, size_t nSamples) {
    if (!samples || nSamples < 4) return;

    // Raw Goertzel magnitudes — bypass computePlaybackSpectrum whose 38× gain saturates
    // all bands to 220 for playback-level PCM. Auto-gain (same as setVibingSpectrum) keeps
    // the bars reactive and frequency-differentiated regardless of music loudness.
    float magRaw[kVibingBands];
    float frameMax = 0.0f;
    for (int b = 0; b < kVibingBands; b++) {
      magRaw[b] = goertzelMagNorm(samples, nSamples, kBandFreqHz[b], 16000.0f) * kBandGain[b];
      if (magRaw[b] > frameMax) frameMax = magRaw[b];
    }
    _vibPcmAgcMax = fmaxf(frameMax, _vibPcmAgcMax * 0.985f);
    if (_vibPcmAgcMax < 20.0f) _vibPcmAgcMax = 20.0f;
    const float agcGate = _vibPcmAgcMax * 0.05f;
    const float agcRange = fmaxf(1.0f, _vibPcmAgcMax - agcGate);

    static float s_pcmBassEnv = 0.0f;
    const float bassRaw = magRaw[0] + magRaw[1] + magRaw[2];
    s_pcmBassEnv = fmaxf(bassRaw * 0.34f, s_pcmBassEnv * 0.86f);
    const float beat = (bassRaw > s_pcmBassEnv * 1.08f && bassRaw > agcGate * 3.0f) ? 1.45f : 1.0f;

    float peak = 0.0f, sum = 0.0f;
    for (int b = 0; b < kVibingBands; b++) {
      const float u = (float)b / (float)(kVibingBands - 1);
      float env = sinf(u * 3.14159265f);
      env = env * env;
      float nrm = (magRaw[b] - agcGate) / agcRange;
      if (nrm < 0.0f) nrm = 0.0f;
      if (nrm > 1.0f) nrm = 1.0f;
      float tgt = nrm * (0.50f + 0.50f * env) * 220.0f;
      if (b < 4) tgt = fminf(tgt * beat, 220.0f);
      const float rate = (tgt > _vibingBands[b]) ? 0.93f : 0.52f;
      _vibingBands[b] += (tgt - _vibingBands[b]) * rate;
      sum += _vibingBands[b];
      if (_vibingBands[b] > peak) peak = _vibingBands[b];
    }
    (void)sum;
    // Mouth openness from the ACTUAL played PCM loudness (RMS), AUTO-GAINED to the recent
    // peak so it uses the full range and clearly reacts: loud syllables open wide, pauses
    // close. The old powf(mix,0.24)*1.25 saturated everything to ~70-100, so the mouth
    // barely moved and looked the same for every sound.
    float sq = 0.0f;
    for (size_t i = 0; i < nSamples; i++) { float s = (float)samples[i]; sq += s * s; }
    float rmsv = sqrtf(sq / (float)nSamples);
    _ampEnvMax = fmaxf(rmsv, _ampEnvMax * 0.990f);
    if (_ampEnvMax < 200.0f) _ampEnvMax = 200.0f;
    float norm = rmsv / _ampEnvMax;
    if (rmsv < 110.0f) norm = 0.0f;                 // hard silence -> mouth closed
    float lvl = powf(norm, 0.80f) * 108.0f;         // mild expansion, peaks reach 100
    setMouthAmplitude((uint8_t)(lvl > 100.0f ? 100.0f : lvl));

    if (_emotion != Emotion::Vibing) {
      _dirty = true;
      return;
    }
    pushVibingHistoryColumn();
    pushVibingHistoryColumn();
    _dirty = true;
  }

  static bool gearHit(int sx, int sy, int screenW) {
    if (screenW <= 250) {
      const int gx = 227, gy = 16;
      int dx = sx - gx, dy = sy - gy;
      return (sx >= 196 && sy <= 64) || ((dx * dx + dy * dy) <= (20 * 20));
    }
    int dx = sx - 303, dy = sy - 16;
    return (sx >= 262 && sy <= 64) || ((dx * dx + dy * dy) <= (20 * 20));
  }

  // Is the touch on the character's body (visor + mouth)? Used for petting / poking him.
  static bool bodyHit(int sx, int sy, int screenW) {
    return sx >= 16 && sx <= screenW - 16 && sy >= 10 && sy <= 196;
  }

  // Brief screen shake — the "got poked / hit" reaction.
  void shake(uint16_t ms = 340) { _shakeUntil = millis() + ms; _dirty = true; }
  bool shaking() const { return millis() < _shakeUntil; }

  void setTalking(bool talking) {
    if (talking != _talking) {
      _talking = talking;
      if (talking) {
        _gazeX = _gazeY = 0;
        _speechCaption.markPlaybackStart();
        drawCaptionStrip();
      } else {
        _mouthAmp = 0;
        _mouthAmpTarget = 0;
        _mouthAmpSmooth = 0;
        _singing = false;
        _nextGazeAt = millis() + 1200;
        _speechCaption.end();
        drawCaptionStrip();
      }
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

  // Talking-mouth style: 0 = equalizer bars, 1 = crossing waves, 2 = lips. From Settings.
  void setMouthAnim(uint8_t a) { if (a > 2) a = 0; if (a != _mouthAnim) { _mouthAnim = a; _dirty = true; } }
  uint8_t mouthAnim() const { return _mouthAnim; }

  void setSpeechCaptionMode(uint8_t mode) { _speechCaption.setMode(mode); }

  // Respuesta hablada (notify, converse, TTS): no texto plano salvo modo ASCII/karaoke.
  void beginReplyCaption(const String &text) {
    _speechCaption.begin(text);
    drawCaptionStrip();
  }

  void endReplyCaption() {
    _speechCaption.end();
    drawCaptionStrip();
  }

  void setMouthAmplitude(uint8_t level) {
    level = level > 100 ? 100 : level;
    const bool same = (level == _mouthAmpTarget);
    _mouthAmpTarget = level;
    if (_talking || _singing || _emotion == Emotion::Vibing) _dirty = true;
    else if (!same) _dirty = true;
  }

  void smoothMouthAmp() {
    if (_singing) {
      _mouthAmpSmooth = 100.0f;
      _mouthAmp = 100;
      return;
    }
    if (!_talking && !_singing && _emotion != Emotion::Vibing && _mouthAmpTarget == 0) {
      _mouthAmpSmooth *= 0.7f;
      if (_mouthAmpSmooth < 0.1f) _mouthAmpSmooth = 0.0f;
      _mouthAmp = (uint8_t)(_mouthAmpSmooth + 0.5f);
      return;
    }
    float target = (float)_mouthAmpTarget;
    float gap = fabsf(target - _mouthAmpSmooth);
    float rate = (target > _mouthAmpSmooth)
      ? (0.88f + fminf(0.1f, gap * 0.009f))
      : (0.55f + fminf(0.4f, gap * 0.014f));
    if (_talking || _singing) {
      rate = (target > _mouthAmpSmooth) ? 0.76f : 0.50f;
    } else if (_emotion == Emotion::Vibing) {
      rate = (target > _mouthAmpSmooth) ? 0.58f : 0.40f;
    }
    _mouthAmpSmooth += (target - _mouthAmpSmooth) * rate;
    if (_mouthAmpSmooth < 0.06f) _mouthAmpSmooth = 0.0f;
    _mouthAmp = (uint8_t)(_mouthAmpSmooth + 0.5f);
  }

  void update() {
    uint32_t now = millis();
    bool changed = false;
    if (now < _shakeUntil) _dirty = true;   // keep redrawing while the shake animates

    if (_emotion != Emotion::Vibing && !_singing && now >= _nextBlinkAt && now >= _blinkEndAt) {
      _blinkEndAt = now + 110 + random(90);
      if (_pendingExtraBlink) {
        _pendingExtraBlink = false;
        _nextBlinkAt = _blinkEndAt + 200 + random(80); // second blink follows quickly
      } else {
        _nextBlinkAt = _blinkEndAt + blinkDelayMs();
      }
      changed = true;
    }
    float blink = 0;
    if (_emotion != Emotion::Vibing && now < _blinkEndAt) {
      float u = (_blinkEndAt - now) / 200.0f;
      float bell = 1.0f - fminf(1.0f, fabsf(u - 0.5f) * 2.0f);
      blink = fminf(1.0f, bell * 1.2f);
      if (blink != _blinkAmt) changed = true;
    } else if (_emotion != Emotion::Vibing && _blinkAmt > 0) {
      changed = true;
    }
    _blinkAmt = blink;

    if (!_singing && _blinkAmt < 0.05f) {
      if (_emotion == Emotion::Thinking && !_talking) {
        int g = ((now / 320) % 2) ? 10 : -10;
        if (g != (int)_gazeTx) { _gazeTx = g; _gazeTy = -3; changed = true; }
      } else if (_emotion != Emotion::Vibing && now >= _nextGazeAt) {
        int amp = _talking ? (gazeAmp() * 6 / 10) : gazeAmp();   // subtler, livelier while talking
        _gazeTx = random(-amp, amp + 1);
        _gazeTy = random(-(amp / 4), amp / 4 + 1);
        _nextGazeAt = now + (_talking ? 480 : 850) + random(_talking ? 1200 : 2300);
        changed = true;
      }
      if (_emotion != Emotion::Vibing) {
        float nx = _gazeX + (_gazeTx - _gazeX) * 0.18f;
        float ny = _gazeY + (_gazeTy - _gazeY) * 0.14f;
        if (fabsf(nx - _gazeX) > 0.3f || fabsf(ny - _gazeY) > 0.3f) {
          _gazeX = nx; _gazeY = ny;
          changed = true;
        }
      }
    }

    if (_talking || _singing) {
      smoothMouthAmp();
      if (_emotion == Emotion::Vibing) {
        tickVibingMotion(now, changed);
      }
      _dirty = true;
    } else if (_emotion == Emotion::Vibing) {
      uint8_t prevMouth = _mouthAmp;
      smoothMouthAmp();
      tickVibingMotion(now, changed);
      const bool mouthMoved = abs((int)_mouthAmp - (int)prevMouth) >= 1;
      const bool bobMoved = fabsf(_vibingBobY - _lastVibingBobDraw) > 0.12f;
      const bool frameDue = (now - _lastVibingDraw) >= 28;
      if (changed || mouthMoved || bobMoved || frameDue) {
        _dirty = true;
        if (bobMoved) _lastVibingBobDraw = _vibingBobY;
        if (frameDue) _lastVibingDraw = now;
      }
    } else {
      if (_mouthAmpTarget > 0 || _mouthAmpSmooth > 0.1f) {
        smoothMouthAmp();
        _dirty = true;
      } else if (changed) {
        _dirty = true;
      }
    }

    // Keep display alive for idle breathing and listening indicator (20 fps)
    if (_listening || (!_talking && !_singing && _emotion != Emotion::Vibing)) {
      if (now - _lastIdleDrawAt >= 50u) { _dirty = true; _lastIdleDrawAt = now; }
    }

    if (_speechCaption.active() && _talking && _speechCaption.mode() == kSpeechCaptionKaraoke) {
      _speechCaption.tick(now, _talking);
      if (_speechCaption.needsRedraw(_captionDrawIdx)) {
        drawCaptionStrip();
      }
    }

    if (_dirty) { draw(); _dirty = false; }
  }

  // Mensajes de estado (Pensando..., errores). No borra caption de respuesta pendiente.
  void showText(const String &text, uint16_t color = TFT_WHITE) {
    int y = FACE_OFFSET_Y + FACE_H + 2;
    _gfx.fillRect(0, y, _gfx.width(), _gfx.height() - y, TFT_BLACK);
    if (text.length() == 0) {
      if (_speechCaption.active()) drawCaptionStrip();
      return;
    }
    int tx = 8 + FACE_OFFSET_X;
    if (tx < 2) tx = 2;
    _gfx.setTextColor(color, TFT_BLACK);
    _gfx.setFont(&fonts::DejaVu18);
    _gfx.setCursor(tx, y + 4);
    _gfx.setTextWrap(true);
    String safe;
    foldSpanishAscii(text, safe);
    if (safe.length() == 0) safe = text;
    _gfx.print(safe);
  }

  void drawCaptionStrip() {
    _speechCaption.draw(_gfx, FACE_OFFSET_Y, FACE_H);
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
  String _topTitle;
  String _topTitleRaw;
  int _topTitleX = 2;
  bool _dirty = true, _talking = false, _singing = false, _showGear = false, _bored = false;
  bool _listening = false;
  uint32_t _lastIdleDrawAt = 0;
  uint8_t _mouthAmp = 0, _mouthAmpTarget = 0;
  uint8_t _mouthAnim = 0;     // talking-mouth style (0 bars, 1 waves, 2 lips)
  float _mouthAmpSmooth = 0;
  float _ampEnvMax = 200.0f;  // slow-decaying RMS peak for auto-gained mouth loudness
  uint32_t _blinkEndAt = 0, _nextBlinkAt = 5000, _nextGazeAt = 4000, _shakeUntil = 0;
  int _faceScreenW = FACE_DESIGN_W;
  float _blinkAmt = 0, _gazeX = 0, _gazeY = 0, _gazeTx = 0, _gazeTy = 0, _browPhase = 0;
  float _browDySmooth = 0.0f, _browTiltSmooth = 0.0f;
  float _intensity = 1.0f;
  bool _pendingExtraBlink = false;
  uint32_t _transitionEnd = 0;
  float _vibingBobY = 0, _lastVibingBobDraw = 0;
  float _vibingLidPulse = 0.5f;
  static constexpr int kVibMouthX0 = 70, kVibMouthX1 = 250;
  static constexpr int kVibMouthY0 = 150, kVibMouthY1 = 186;
  uint8_t _vibingMicGain = 150;
  uint16_t _vibingFloor = 100;
  uint16_t _vibingCeil = 500;
  float _vibAgcMax    = 30.0f;   // auto-gain for mic path (setVibingSpectrum)
  float _vibPcmAgcMax = 20.0f;   // auto-gain for playback path (feedPlaybackPcm) — separate to avoid cross-contamination
  static constexpr int kVibingBands = 12;
  static constexpr int kSpecCols = 20;
  float _vibingBands[kVibingBands]{};
  uint8_t _colHist[kSpecCols]{};
  static constexpr int kVibingNotes = 4;
  float _noteX[kVibingNotes]{}, _noteY[kVibingNotes]{}, _noteVx[kVibingNotes]{}, _noteVy[kVibingNotes]{};
  uint8_t _noteKind[kVibingNotes]{};
  uint32_t _lastNoteSpawn = 0, _lastVibingDraw = 0;
  SpeechCaption _speechCaption;
  uint8_t _captionDrawIdx = 255;

  float vibingHighTone() const {
    float peak = 0.0f, sum = 0.0f;
    const int i0 = 7;
    for (int b = i0; b < kVibingBands; b++) {
      sum += _vibingBands[b];
      if (_vibingBands[b] > peak) peak = _vibingBands[b];
    }
    const float avg = sum / (float)(kVibingBands - i0);
    float mix = peak * 0.72f + avg * 0.28f;
    float lvl = mix / 220.0f;
    return lvl > 1.0f ? 1.0f : lvl;
  }

  void tickVibingMotion(uint32_t now, bool &changed) {
    float t = now * 0.001f;
    float amp = _mouthAmpSmooth / 100.0f;
    amp = fmaxf(amp, 0.07f + 0.04f * sinf(t * 2.0f));
    const float beatHz = 1.6f + amp * 1.4f;
    const float bobHz = beatHz * 0.68f;
    const float targetBob = sinf(t * bobHz * 6.2831853f) * (0.5f + amp * 1.6f);
    const float prev = _vibingBobY;
    _vibingBobY += (targetBob - _vibingBobY) * 0.09f;
    if (fabsf(_vibingBobY - prev) > 0.04f) changed = true;

    const float beat = sinf(t * beatHz * 6.2831853f);
    const float lidTarget = 0.5f + 0.5f * beat;
    const float prevLid = _vibingLidPulse;
    _vibingLidPulse += (lidTarget - _vibingLidPulse) * 0.12f;

    const float gazeAmpX = 2.2f + amp * 3.0f;
    const float targetGx = sinf(t * beatHz * 6.2831853f) * gazeAmpX;
    const float prevGx = _gazeX;
    _gazeX += (targetGx - _gazeX) * 0.08f;
    _gazeY += (0.0f - _gazeY) * 0.12f;

    if (fabsf(_vibingLidPulse - prevLid) > 0.015f || fabsf(_gazeX - prevGx) > 0.15f) {
      changed = true;
    }
  }

  static uint16_t lerp565(uint16_t a, uint16_t b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    int r1 = (a >> 11) & 0x1F, g1 = (a >> 5) & 0x3F, b1 = a & 0x1F;
    int r2 = (b >> 11) & 0x1F, g2 = (b >> 5) & 0x3F, b2 = b & 0x1F;
    int r = r1 + (int)((r2 - r1) * t);
    int g = g1 + (int)((g2 - g1) * t);
    int bl = b1 + (int)((b2 - b1) * t);
    return (uint16_t)((r << 11) | (g << 5) | bl);
  }

  uint16_t flickerInk(uint16_t base, uint16_t bright) const {
    float t = millis() * 0.001f;
    float flick = 0.88f + 0.12f * sinf(t * 17.0f + _browPhase);
    if ((millis() & 0xFF) < 4) flick *= 0.82f;
    return lerp565(base, bright, flick);
  }

  uint16_t accentInk() const { return flickerInk(INK, INK_BRIGHT); }

  // Mouth LED tint only — eyes and visor stay cyan/white.
  void mouthLedInk(uint16_t &seg, uint16_t &glow, uint16_t &hi) const {
    seg = accentInk();
    glow = INK_GLOW;
    hi = INK_BRIGHT;
    switch (_emotion) {
      case Emotion::Angry:
        seg = 0xFC60; glow = 0x4208; hi = 0xFE20; break;
      case Emotion::Love:
        seg = 0xF99F; glow = 0x4A08; hi = 0xFDCF; break;
      case Emotion::Sad:
        seg = 0x4DFF; glow = 0x0218; hi = 0x6DFF; break;
      case Emotion::Sleepy:
        seg = 0x2DFF; glow = 0x0210; hi = 0x3DFF; break;
      case Emotion::Confused:
      case Emotion::Dizzy:
        seg = 0xB81B; glow = 0x3008; hi = 0xD81F; break;
      default:
        break;
    }
  }

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
        p.brow = Brow::Raise; p.eyeR = 38; p.pupilS = 7;
        p.mouth = {96, 224, 22, 6, 3, 5, 0, 0, 0};
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
      case Emotion::Vibing:
        p.brow = Brow::Arch;
        p.topLid = 0.06f;
        p.botLid = 0.20f;
        p.mouth = {78, 242, 25, 8, 3, 14, 0, 0, 0};
        break;
      default: // Neutral
        p.topLid = 0.12f;
        p.mouth = {88, 232, 19, 4, 3, 2, 0, 0, 0};
        break;
    }
    return p;
  }

  static constexpr int VISOR_CLIP_X = 44, VISOR_CLIP_Y = 38, VISOR_CLIP_W = 232, VISOR_CLIP_H = 60;

  void drawVisorScanlines(int dy = 0) {
    for (int y = 36 + dy; y < 100 + dy; y += 3) {
      _canvas.drawFastHLine(40, y, 240, SCANLINE);
    }
  }

  void drawCapsule(int dy = 0) {
    const uint32_t now = millis();
    uint16_t frame, glow;
    if (_listening) {
      // Pulsing green border while recording (~1.1 s period)
      float ph = (sinf(now * 0.0055f) + 1.0f) * 0.5f;
      glow  = lerp565(0x0340, 0x03E0, ph);   // dark green → mid green
      frame = lerp565(0x03E0, 0x07E0, ph);   // mid green → bright green
    } else {
      if (!_talking && !_singing) {
        // Idle breathing glow (~4 s cycle) — visibly dramatic range
        float breath = (sinf(now * 0.00157f) + 1.0f) * 0.5f;
        glow  = lerp565(INK_GLOW, INK_BRIGHT, breath * 0.82f + 0.08f);
        frame = lerp565(INK, INK_BRIGHT, breath * 0.55f);
      } else {
        frame = accentInk();
        glow  = lerp565(INK_GLOW, INK, 0.55f);
      }
    }
    _canvas.fillRoundRect(36, 32 + dy, 248, 72, 36, VISOR);
    _canvas.fillRoundRect(44, 38 + dy, 232, 60, 30, VISOR_DEEP);
    drawVisorScanlines(dy);
    _canvas.drawRoundRect(21, 17 + dy, 278, 102, 50, glow);
    _canvas.drawRoundRect(23, 19 + dy, 274, 98, 49, glow);
    _canvas.drawRoundRect(24, 20 + dy, 272, 96, 48, frame);
  }

  void drawBrows(const Preset &p, float yOff, float tilt) {
    struct Pair { int a, b; };
    const Pair pairs[2] = {{62, 136}, {184, 258}};
    for (const auto &pr : pairs) {
      int a = pr.a, b = pr.b;
      int y0 = 46 + (int)yOff, y1 = 46 + (int)yOff;
      switch (p.brow) {
        case Brow::Angry:
          _canvas.drawLine(a, 48 + (int)yOff, b, (b < 200 ? 58 : 48) + (int)yOff + (int)tilt, accentInk());
          continue;
        case Brow::Sad:
          _canvas.drawLine(a, 58 + (int)yOff, b, (b < 200 ? 46 : 58) + (int)yOff + (int)tilt, accentInk());
          continue;
        case Brow::Raise:
          _canvas.drawLine(a - 4, 34 + (int)yOff, b + 4, 34 + (int)yOff + (int)tilt, accentInk());
          continue;
        case Brow::Arch: {
          int cx = (a + b) / 2;
          for (int i = 0; i < 8; i++) {
            float t0 = i / 8.0f, t1 = (i + 1) / 8.0f;
            int x0 = a + (int)((b - a) * t0), x1 = a + (int)((b - a) * t1);
            int ya = y0 + (int)(-(1 - (2 * fabsf(t0 - 0.5f))) * 8);
            int yb = y0 + (int)(-(1 - (2 * fabsf(t1 - 0.5f))) * 8);
            _canvas.drawLine(x0, ya, x1, yb + (int)tilt, accentInk());
          }
          continue;
        }
        case Brow::Asym:
          if (a < 150) _canvas.drawLine(a, 48 + (int)yOff, b, 40 + (int)yOff + (int)tilt, accentInk());
          else _canvas.drawLine(a, 58 + (int)yOff, b, 50 + (int)yOff - (int)tilt, accentInk());
          continue;
        default:
          _canvas.drawLine(a, y0, b, y1 + (int)tilt, accentInk());
      }
    }
  }

  void drawLidMask(int cx, int cy, int r, float top, float bot, LidAng ang) {
    const uint16_t lid = VISOR_DEEP;
    if (ang == LidAng::AngryIn) {
      bool inner = cx > CXC;
      if (!inner) _canvas.fillTriangle(cx - r, cy - r, cx + r + 2, cy - r, cx + r + 2, cy - r + (int)(r * 0.55f), lid);
      else _canvas.fillTriangle(cx + r, cy - r, cx - r - 2, cy - r, cx - r - 2, cy - r + (int)(r * 0.55f), lid);
    } else if (ang == LidAng::SadOut) {
      bool outer = cx < CXC;
      if (outer) _canvas.fillTriangle(cx - r, cy + r, cx + r, cy + r, cx + r, cy + r - (int)(r * 0.5f), lid);
      else _canvas.fillTriangle(cx + r, cy + r, cx - r, cy + r, cx - r, cy + r - (int)(r * 0.5f), lid);
    }
    if (top > 0) _canvas.fillRect(cx - r - 1, cy - r - 1, r * 2 + 2, (int)(r * 2 * top) + 1, lid);
    if (bot > 0) {
      int h = (int)(r * 2 * bot);
      _canvas.fillRect(cx - r - 1, cy + r - h, r * 2 + 2, h + 1, lid);
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

  // Sample the playback spectrum at fractional position u (0 = low freq .. 1 = high freq).
  float bandAtU(float u) const {
    if (u < 0) u = 0;
    if (u > 1) u = 1;
    float bf = u * (kVibingBands - 1);
    int b0 = (int)bf;
    int b1 = (b0 + 1 < kVibingBands) ? b0 + 1 : b0;
    float fr = bf - b0;
    return _vibingBands[b0] * (1.0f - fr) + _vibingBands[b1] * fr;
  }
  float bandPeak() const {
    float pk = 1.0f;
    for (int b = 0; b < kVibingBands; b++) if (_vibingBands[b] > pk) pk = _vibingBands[b];
    return pk;
  }

  // Style 0 — equalizer bars. Each bar is normalised to the CURRENT peak band (frame-
  // relative), so the loud mid-frequencies of speech don't pin the centre bars at the top:
  // the tallest bar shifts with each phoneme and overall loudness sets how open it is.
  void drawMouthBars(int ix, int iy, int iw, int ih, int segs, uint16_t ink, uint16_t glow, uint16_t hi) {
    if (segs < 3) segs = 3;
    if (segs > 9) segs = 9;
    const float amp = _mouthAmpSmooth / 100.0f;    // overall loudness (auto-gained) -> openness
    const float pk = bandPeak();
    const int gap = 3;
    const int bw = (iw - gap * (segs - 1)) / segs;
    int sx = ix;
    for (int i = 0; i < segs; i++) {
      float u = (segs > 1) ? (float)i / (segs - 1) : 0.5f;
      float rel = bandAtU(u) / pk;                 // 0..1 spectral shape (per-bar variety)
      float frac = amp * (0.30f + 0.70f * rel);    // amp drives the open, shape adds the dance
      if (frac < 0.05f) frac = 0.05f;
      if (frac > 1.0f) frac = 1.0f;
      int sh = (int)(ih * frac);
      if (sh < 3) sh = 3;
      int sy = iy + (ih - sh) / 2;
      _canvas.fillRoundRect(sx - 1, sy - 1, bw + 2, sh + 2, 3, glow);
      _canvas.fillRoundRect(sx, sy, bw, sh, 2, ink);
      if (frac > 0.55f) _canvas.drawFastHLine(sx + 1, sy + 1, bw - 2, hi);
      sx += bw + gap;
    }
  }

  // Style 1 — two crossing waves: one driven by the LOW bands, one by the HIGH bands, with
  // different spatial frequency and opposite scroll so they weave across each other.
  void drawMouthWaves(int ix, int iy, int iw, int ih, uint16_t ink, uint16_t glow, uint16_t hi) {
    const int cy = iy + ih / 2;
    const int half = kVibingBands / 2;
    float lo = 0, hg = 0;
    for (int b = 0; b < half; b++) lo += _vibingBands[b];
    for (int b = half; b < kVibingBands; b++) hg += _vibingBands[b];
    // Both wave heights are driven by the overall loudness (so they react to the sound and
    // go flat in pauses), each weighted toward its low/high band so the two move differently.
    const float amp = _mouthAmpSmooth / 100.0f;
    const float tot = lo + hg + 1.0f;
    float aA = amp * (0.45f + 1.10f * (lo / tot));
    float aB = amp * (0.45f + 1.10f * (hg / tot));
    if (aA > 1.0f) aA = 1.0f;
    if (aB > 1.0f) aB = 1.0f;
    const float t = millis() * 0.006f;
    const float ampPx = ih * 0.44f;
    int pax = ix, pay = cy, pbx = ix, pby = cy;
    for (int x = ix; x <= ix + iw; x++) {
      float u = (float)(x - ix) / iw;
      int yA = cy + (int)(sinf(u * 3.14159265f * 3.0f + t * 1.3f) * ampPx * aA);
      int yB = cy + (int)(sinf(u * 3.14159265f * 4.0f - t * 1.0f) * ampPx * aB);
      if (x > ix) {
        _canvas.drawLine(pax, pay, x, yA, hi);     // low-frequency wave (bright)
        _canvas.drawLine(pbx, pby, x, yB, glow);   // high-frequency wave (dim)
      }
      pax = x; pay = yA; pbx = x; pby = yB;
    }
  }

  // Style 2 — Bender grid: two horizontal lines bound the teeth grid. They DON'T travel
  // sideways; they bow with a FIXED, centre-weighted profile (peaks in the middle, fades to
  // nothing at the sides) whose height rises and falls with the loudness/syllables. Top and
  // bottom mirror each other so the grille flexes open at the centre. No jaw, no lips, no
  // teeth moving on their own — a flexible metal grille that flexes while speaking.
  void drawMouthLips(int ix, int iy, int iw, int ih, uint16_t ink, uint16_t glow, uint16_t hi) {
    (void)hi;
    const float amp = _mouthAmpSmooth / 100.0f;
    const int cy = iy + ih / 2;
    const float halfGap = 3.0f + ih * 0.10f * amp;      // ~6 px at rest, opens more with volume
    const float waveAmp = ih * (0.02f + 0.22f * amp);   // centre rise; stronger reaction to sound
    const int teeth = 6;

    int ptx = ix, pty = 0, pbx = ix, pby = 0;
    for (int x = ix; x <= ix + iw; x++) {
      float u = (float)(x - ix) / iw;                   // 0..1 across the mouth (fixed, no scroll)
      float env = sinf(3.14159265f * u);                // rounded (flat) centre, sides fade
      float base = waveAmp * env;
      // slight fixed deformation near the centre peak, different on each line so the two
      // aren't a perfect mirror (a touch more organic / robotic, not a clean math curve).
      float defT = waveAmp * env * 0.22f * sinf(3.14159265f * u * 5.0f);
      float defB = waveAmp * env * 0.15f * sinf(3.14159265f * u * 4.0f + 0.7f);
      int topY = cy - (int)halfGap - (int)(base + defT);   // top rises at the centre
      int botY = cy + (int)halfGap + (int)(base + defB);   // bottom drops at the centre (mirror)
      if (x > ix) {
        _canvas.drawLine(ptx, pty, x, topY, ink);          // top line, 3 px thick
        _canvas.drawLine(ptx, pty + 1, x, topY + 1, ink);
        _canvas.drawLine(ptx, pty + 2, x, topY + 2, ink);
        _canvas.drawLine(pbx, pby, x, botY, ink);          // bottom line, 3 px thick
        _canvas.drawLine(pbx, pby - 1, x, botY - 1, ink);
        _canvas.drawLine(pbx, pby - 2, x, botY - 2, ink);
      }
      ptx = x; pty = topY; pbx = x; pby = botY;
    }
    for (int i = 0; i <= teeth; i++) {                   // vertical teeth follow the bow
      int x = ix + (iw * i) / teeth;
      float u = (float)(x - ix) / iw;
      float env = sinf(3.14159265f * u);
      float base = waveAmp * env;
      float defT = waveAmp * env * 0.22f * sinf(3.14159265f * u * 5.0f);
      float defB = waveAmp * env * 0.15f * sinf(3.14159265f * u * 4.0f + 0.7f);
      int ty = cy - (int)halfGap - (int)(base + defT);
      int by = cy + (int)halfGap + (int)(base + defB);
      _canvas.drawLine(x, ty, x, by, glow);                 // teeth, 2 px thick
      _canvas.drawLine(x - 1, ty, x - 1, by, glow);
    }
  }

  // Bender-style LED segment mouth. While talking it uses the style chosen in Settings; the
  // music "Vibing" visualizer and the static (idle) mouth keep the classic segment bars.
  void drawLedMouth(bool animate, float bowMul = 0.0f, int segCount = 7, int dy = 0) {
    const int x0 = 78, x1 = 242, y0 = 138 + dy, y1 = 172 + dy;
    const int pad = 5;
    uint16_t segInk, segGlow, segHi;
    mouthLedInk(segInk, segGlow, segHi);
    _canvas.fillRoundRect(x0, y0, x1 - x0, y1 - y0, 10, VISOR_DEEP);
    _canvas.drawRoundRect(x0, y0, x1 - x0, y1 - y0, 10, segInk);
    const int ix = x0 + pad, iy = y0 + pad;
    const int iw = x1 - x0 - pad * 2, ih = y1 - y0 - pad * 2;

    if (animate && _emotion != Emotion::Vibing) {
      if (_mouthAnim == 1)      drawMouthWaves(ix, iy, iw, ih, segInk, segGlow, segHi);
      else if (_mouthAnim == 2) drawMouthLips(ix, iy, iw, ih, segInk, segGlow, segHi);
      else                      drawMouthBars(ix, iy, iw, ih, segCount, segInk, segGlow, segHi);
      return;
    }

    const int segs = segCount, gap = 3, troughH = ih, innerW = iw;
    const int segW = (innerW - gap * (segs - 1)) / segs;
    int sx = ix;
    float amp = animate ? (_mouthAmpSmooth / 100.0f) : 0.0f;
    if (_emotion == Emotion::Vibing && animate) amp = powf(amp, 0.48f);
    for (int i = 0; i < segs; i++) {
      float u = (float)i / (segs - 1);
      float env = sinf(u * 3.14159265f);
      env = env * env;
      float bow = bowMul * sinf(u * 3.14159265f) * 0.35f;
      int sh;
      if (animate) {
        sh = (int)(troughH * (0.08f + 0.92f * amp * env));
        if (sh < 4) sh = 4;
      } else {
        sh = (int)(troughH * (0.50f + 0.50f * env + bow));
      }
      if (sh > troughH) sh = troughH;
      int sy = iy + (troughH - sh) / 2;
      _canvas.fillRoundRect(sx - 1, sy - 1, segW + 2, sh + 2, 4, segGlow);
      _canvas.fillRoundRect(sx, sy, segW, sh, 3, segInk);
      if (animate && _emotion == Emotion::Vibing && amp > 0.25f) {
        _canvas.drawFastHLine(sx + 1, sy + 1, segW - 2, segHi);
      } else if (!animate && i % 2 == 0) {
        _canvas.drawFastHLine(sx + 1, sy + 1, segW - 2, segHi);
      }
      sx += segW + gap;
    }
  }

  void drawLedMouthForEmotion(Emotion e, bool animate, const MouthCfg &m) {
    float bow = 0.0f;
    int segs = 7;
    switch (e) {
      case Emotion::Neutral:   segs = 7; bow =  0.10f; break; // calm hint of a smile
      case Emotion::Happy:
      case Emotion::Excited:
      case Emotion::Love:      segs = 9; bow =  0.40f; break; // wide open smile
      case Emotion::Sad:       segs = 6; bow = -0.30f; break; // deep frown
      case Emotion::Angry:     segs = 4; bow = -0.25f; break; // tight aggressive frown
      case Emotion::Surprised: segs = 9; bow =  0.10f; break; // wide, slightly open
      case Emotion::Thinking:  segs = 6; bow = -0.10f; break; // pondering frown
      case Emotion::Sleepy:    segs = 5; bow = -0.14f; break; // heavy droopy frown
      case Emotion::Cool:      segs = 5; bow =  0.22f; break; // confident smirk
      case Emotion::Confused:  segs = 8; bow =  0.04f; break; // wide, puzzled
      case Emotion::Dizzy:     segs = 6; bow = -0.20f; break; // wonky frown
      case Emotion::Vibing:    segs = 7; bow =  0.30f; break;
      default:                 segs = 7; bow =  0.08f; break;
    }
    // Scale expression toward neutral (bow=0.10, segs=7) at lower intensity.
    bow  = 0.10f + (bow - 0.10f) * _intensity;
    segs = 7 + (int)roundf((float)(segs - 7) * _intensity);
    drawLedMouth(animate, bow, segs);
  }

  void drawHeartPupil(int px, int py, int s) {
    int hs = (int)(s * 1.08f);
    _canvas.fillCircle(px - hs / 3, py - hs / 6, hs / 2 + 1, RED);
    _canvas.fillCircle(px + hs / 3, py - hs / 6, hs / 2 + 1, RED);
    _canvas.fillTriangle(px - hs, py, px + hs, py, px, py + hs, RED);
  }

  void drawSpiralPupil(int cx, int cy) {
    uint32_t t = millis();
    const int steps = 76;                    // more segments = smoother spiral
    int px0 = cx, py0 = cy;
    for (int i = 1; i <= steps; i++) {
      float a = i / (float)steps * 2.6f * 2.0f * PI + t * 0.004f;
      float rad = 2.0f + i * 0.26f;
      int px = cx + (int)(cosf(a) * rad);
      int py = cy + (int)(sinf(a) * rad);
      _canvas.drawLine(px0, py0, px, py, PURPLE);          // ~2 px thick
      _canvas.drawLine(px0 + 1, py0, px + 1, py, PURPLE);
      _canvas.drawLine(px0, py0 + 1, px, py + 1, PURPLE);
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
  static constexpr float kMouthTalkAmpCenter = 0.22f;
  static constexpr float kMouthTalkAmpScale = 2.65f;
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
        _canvas.drawLine(x0, y0, q.x, q.y, accentInk());
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
      _canvas.drawLine(px0, py0, px, py, accentInk());
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

    _canvas.drawLine(m.x0, shapeLtop, m.x0, shapeLbot, accentInk());
    _canvas.drawLine(m.x1, shapeRtop, m.x1, shapeRbot, accentInk());
    int pT = shapeLtop, pB = shapeLbot;
    for (int x = m.x0 + 1; x <= m.x1; x++) {
      int topY, botY;
      mouthShapeAt(x, m, my, w, topY, botY);
      _canvas.drawLine(x - 1, pT, x, topY, accentInk());
      _canvas.drawLine(x - 1, pB, x, botY, accentInk());
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
      _canvas.drawLine(x, topY, x, botY, accentInk());
    }
    for (int vx : {xVertL, xVertR}) {
      int topY, botY;
      mouthShapeAt(vx, m, my, w, topY, botY);
      _canvas.drawLine(vx, topY, vx, botY, accentInk());
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
      a = fminf(1.0f, a * 1.25f);
      a = a * (2.0f - a);
      talkOpen = 0.15f + a * 4.8f;
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
        _canvas.drawLine(x, topY, x, botY, accentInk());
      } else {
        _canvas.drawLine(x - 1, pT, x, topY, accentInk());
        _canvas.drawLine(x - 1, pB, x, botY, accentInk());
      }
      pT = topY;
      pB = botY;
    }

    for (int i = 1; i < m.teeth; i++) {
      int x = m.x0 + (w * i) / m.teeth;
      int topY, botY;
      mouthShapeAt(x, m, my, w, topY, botY);
      _canvas.drawLine(x, topY, x, botY, accentInk());
    }

    for (int ri = 0; ri < kInnerRows; ri++) {
      int lx = m.x0;
      int ly = mouthRowYAt(lx, m, my, w, kInnerDiv[ri], gridRows, talkOpen, animMouth);
      for (int x = m.x0 + 1; x <= m.x1; x++) {
        int hy = mouthRowYAt(x, m, my, w, kInnerDiv[ri], gridRows, talkOpen, animMouth);
        _canvas.drawLine(lx, ly, x, hy, accentInk());
        lx = x;
        ly = hy;
      }
    }
  }

  void drawMouthGrid(const MouthCfg &m) {
    const bool anim = _talking || _singing || _mouthAmpSmooth > 2.0f;
    drawLedMouthForEmotion(_emotion, anim, m);
  }

  void drawHappyClosedEye(int cx, int cy, int r, float squint, float ampLift) {
    const uint16_t ink = accentInk();
    const float arcH = 0.34f * (1.0f - squint * 0.58f);
    const float arcW = 0.82f * (1.0f - squint * 0.06f);
    const int yBase = 6 - (int)(ampLift * 4.0f + squint * 3.0f);
    for (int w = 0; w < 2; w++) {
      for (int i = 0; i < 14; i++) {
        float t0 = i / 14.0f, t1 = (i + 1) / 14.0f;
        float a0 = 3.14159265f * (0.12f + t0 * 0.76f);
        float a1 = 3.14159265f * (0.12f + t1 * 0.76f);
        int x0 = cx + (int)(cosf(a0) * r * arcW);
        int y0 = cy + (int)(sinf(a0) * r * arcH) + yBase + w;
        int x1 = cx + (int)(cosf(a1) * r * arcW);
        int y1 = cy + (int)(sinf(a1) * r * arcH) + yBase + w;
        _canvas.drawLine(x0, y0, x1, y1, ink);
      }
    }
  }

  void drawMusicNoteGlyph(int x, int y, int headR, bool flagged) {
    const uint16_t col = accentInk();
    _canvas.fillCircle(x, y, headR, col);
    const int stemH = headR * 4;
    _canvas.drawFastVLine(x + headR, y - stemH + headR, stemH, col);
    if (flagged) {
      _canvas.fillTriangle(x + headR, y - stemH + headR,
                           x + headR + headR * 2, y - stemH + headR + headR,
                           x + headR, y - stemH + headR + headR * 2, col);
    }
  }

  void drawVibingNotes(int dy) {
    const float amp = _mouthAmpSmooth / 100.0f;
    const float hi = vibingHighTone();
    const float noteDrive = fmaxf(amp * 0.22f, hi);
    const uint32_t now = millis();
    if (noteDrive >= 0.08f) {
      const uint32_t spawnMs = (uint32_t)fmaxf(70.0f, 410.0f - noteDrive * 300.0f);
      const int spawns = (hi > 0.52f) ? 2 : 1;
      if (now - _lastNoteSpawn >= spawnMs) {
        _lastNoteSpawn = now;
        for (int s = 0; s < spawns; s++) {
          for (int i = 0; i < kVibingNotes; i++) {
            if (_noteY[i] < -30.0f) {
              _noteX[i] = 118.0f + random(0, 85);
              _noteY[i] = (float)(kVibMouthY0 - 18) + dy + random(0, 12);
              _noteVy[i] = -1.5f - noteDrive * 2.9f - random(0, 8) * 0.12f;
              _noteVx[i] = (random(0, 2) ? 1.0f : -1.0f) * (0.5f + random(0, 10) * 0.07f);
              _noteKind[i] = (hi > 0.38f && random(0, 3) != 0) ? 1 : (uint8_t)random(0, 2);
              break;
            }
          }
        }
      }
    }
    for (int i = 0; i < kVibingNotes; i++) {
      if (_noteY[i] < -35.0f || _noteY[i] > 210.0f) continue;
      _noteX[i] += _noteVx[i];
      _noteY[i] += _noteVy[i];
      int headR = 4 + (int)(noteDrive * 3.5f);
      if (headR > 7) headR = 7;
      drawMusicNoteGlyph((int)_noteX[i], (int)_noteY[i], headR, _noteKind[i] != 0);
    }
  }

  // Double buffer (LovyanGFX): the whole face is composed off-screen in _canvas
  // (PSRAM sprite) and blitted in ONE opaque pushSprite. A single write per pixel
  // at a fixed position fully overwrites the previous frame, so there is no erase
  // step, no chroma key, hence no flicker and no motion trails. The settings gear
  // sits in the dead zone to the right of the sprite; we clip it out of the blit
  // so the opaque face never blackens it (it is repainted on top afterwards).
  void pushFaceSprite() {
    int shx = 0, shy = 0;                       // hit/poke shake
    if (millis() < _shakeUntil) {
      uint32_t m = millis();
      shx = (int)(sinf(m * 0.9f) * 6.0f);
      shy = (int)(cosf(m * 1.3f) * 3.0f);
    }
    const int gearCx = (_faceScreenW <= 250) ? 227 : 303;
    const int gearLeft = gearCx - 16;
    if (_showGear) _gfx.setClipRect(0, 0, gearLeft, _gfx.height());
    if (_faceScreenW < FACE_DESIGN_W) {
      const float zoom = (float)_faceScreenW / (float)FACE_DESIGN_W;
      _canvas.pushRotateZoom(_faceScreenW * 0.5f + FACE_OFFSET_X + shx,
                             FACE_H * 0.5f + FACE_OFFSET_Y + shy, 0.0f, zoom, zoom);
    } else {
      _canvas.pushSprite(FACE_OFFSET_X + shx, FACE_OFFSET_Y + shy);
    }
    if (_showGear) _gfx.clearClipRect();
  }

  void vibingBarColors(int cx, float t, uint16_t &glow, uint16_t &bot, uint16_t &top, uint16_t &hi) const {
    const float u = (float)cx / (float)(kSpecCols - 1);
    const float pulse = 0.82f + 0.18f * sinf(millis() * 0.011f + cx * 0.42f);
    uint16_t base;
    if (u < 0.33f) {
      base = lerp565(0x0218, 0x07FF, u / 0.33f);
    } else if (u < 0.66f) {
      base = lerp565(0x07FF, 0x5FE0, (u - 0.33f) / 0.33f);
    } else {
      base = lerp565(0x5FE0, 0xF81F, (u - 0.66f) / 0.34f);
    }
    const float e = t * pulse;
    glow = lerp565(VISOR_DEEP, base, 0.30f + e * 0.55f);
    bot = lerp565(base, INK_BRIGHT, e * 0.72f);
    top = lerp565(base, VISOR_DEEP, 0.28f + (1.0f - e) * 0.22f);
    hi = lerp565(INK_BRIGHT, 0xFFFF, e * 0.65f);
  }

  void drawVibingMirrorBar(int cx, int sx, int barW, int midY, int halfH, float t) {
    if (halfH < 2) halfH = 2;
    const int totalH = halfH * 2;
    uint16_t glow, bot, top, hi;
    vibingBarColors(cx, t, glow, bot, top, hi);
    _canvas.fillRoundRect(sx - 1, midY - halfH - 1, barW + 2, totalH + 2, 3, glow);
    _canvas.fillRoundRect(sx, midY - halfH, barW, halfH, 2, top);
    _canvas.fillRoundRect(sx, midY, barW, halfH, 2, bot);
    if (t > 0.28f) {
      _canvas.drawFastHLine(sx + 1, midY, barW - 2, hi);
    }
    if (t > 0.62f && halfH > 3) {
      _canvas.drawFastHLine(sx + 1, midY - halfH + 1, barW - 2, hi);
    }
  }

  // LED equalizer waterfall — barras simétricas (espejo) desde el centro.
  void drawVibingSpectrogramMouth(int dy) {
    const int x0 = kVibMouthX0, x1 = kVibMouthX1;
    const int y0 = kVibMouthY0 + dy, y1 = kVibMouthY1 + dy;
    const int pad = 5, gap = 2;
    uint16_t segInk, segGlow, segHi;
    mouthLedInk(segInk, segGlow, segHi);
    (void)segGlow;
    (void)segHi;
    _canvas.fillRoundRect(x0, y0, x1 - x0, y1 - y0, 10, VISOR_DEEP);
    _canvas.drawRoundRect(x0, y0, x1 - x0, y1 - y0, 10, segInk);

    const int innerW = x1 - x0 - pad * 2;
    const int innerH = y1 - y0 - pad * 2;
    const int barW = (innerW - gap * (kSpecCols - 1)) / kSpecCols;
    const int gridW = barW * kSpecCols + gap * (kSpecCols - 1);
    const int mouthCx = (x0 + x1) / 2;
    const int ox = mouthCx - gridW / 2;
    const int oy = y0 + pad;
    const int midY = oy + innerH / 2;
    const int maxHalf = innerH / 2;

    for (int cx = 0; cx < kSpecCols; cx++) {
      const uint8_t v = vibingColumnAt(cx);
      if (v >= 1) {
        float t = powf(v / 220.0f, 0.38f);
        if (t > 1.0f) t = 1.0f;
        t = fminf(1.0f, t * 1.35f);
        int halfH = (int)(maxHalf * t);
        if (halfH < 2 && v >= 6) halfH = 2;
        const int sx = ox + cx * (barW + gap);
        drawVibingMirrorBar(cx, sx, barW, midY, halfH, t);
      }
    }
    _canvas.drawRoundRect(x0, y0, x1 - x0, y1 - y0, 10, segInk);
  }

  static inline float vibingLidBoundaryY(int cx, int cy, int r, float drop, float arch, int x) {
    const float nx = (float)(x - cx) / (float)r;
    return (cy - r) + 2.0f * r * drop + arch * r * (1.0f - nx * nx);
  }

  // Ojo relajado: rendija blanca; párpado superior arqueado hacia abajo; sin tapa inferior recta.
  void drawVibingRelaxedEye(int cx, int cy, int r, float lidPulse, float gazeX) {
    _canvas.fillCircle(cx, cy, r + 2, VISOR_DEEP);

    const float drop = 0.40f + lidPulse * 0.06f;
    const float arch = 0.26f + lidPulse * 0.06f;

    for (int y = cy - r; y <= cy + r; y++) {
      const int disc = r * r - (y - cy) * (y - cy);
      if (disc <= 0) continue;
      const int halfW = (int)sqrtf((float)disc);
      const int xL = cx - halfW;
      const int xR = cx + halfW;
      int x = xL;
      while (x <= xR) {
        const float bnd = vibingLidBoundaryY(cx, cy, r, drop, arch, x);
        if ((float)y + 0.5f < bnd) {
          const int rs = x;
          while (x <= xR && (float)y + 0.5f < vibingLidBoundaryY(cx, cy, r, drop, arch, x)) x++;
          _canvas.fillRect(rs, y, x - rs, 1, VISOR_DEEP);
        } else {
          const int rs = x;
          while (x <= xR && (float)y + 0.5f >= vibingLidBoundaryY(cx, cy, r, drop, arch, x)) x++;
          _canvas.fillRect(rs, y, x - rs, 1, EYE);
        }
      }
    }

    _canvas.drawCircle(cx, cy, r + 1, EYE_GLOW);
    drawSquarePupil(cx + (int)gazeX, cy + 5, 5);
  }

  void drawVibingEyes(int bob) {
    const int yL = EYE_Y + bob;
    const int yR = EYE_Y + bob;
    drawVibingRelaxedEye(EYE_L, yL, EYE_RAD, _vibingLidPulse, _gazeX);
    drawVibingRelaxedEye(EYE_R, yR, EYE_RAD, _vibingLidPulse, _gazeX);
  }

  void drawVibingFace() {
    const int bob = (int)(_vibingBobY + 0.5f);
    const float amp = _mouthAmpSmooth / 100.0f;
    const float t = millis() * 0.001f;
    const float beatHz = 1.6f + amp * 1.4f;
    const float browDy = sinf(t * beatHz * 0.68f * 6.2831853f) * (0.4f + amp * 0.9f);
    const float browTilt = sinf(t * 2.2f + _browPhase) * (0.2f + amp * 0.4f);

    _canvas.fillSprite(BG);
    drawCapsule(bob);
    _canvas.setClipRect(VISOR_CLIP_X, VISOR_CLIP_Y + bob, VISOR_CLIP_W, VISOR_CLIP_H);
    drawVibingEyes(bob);
    _canvas.clearClipRect();

    Preset bp;
    bp.brow = Brow::Arch;
    drawBrows(bp, browDy, browTilt);

    drawVibingSpectrogramMouth(bob);
    drawVibingNotes(bob);
  }

  void drawCompactMouth() {
    const int cx = 160, cy = 154, hw = 28, hh = 17;
    const uint16_t mInk = accentInk();
    _canvas.fillRoundRect(cx - hw, cy - hh, hw * 2, hh * 2, 6, VISOR_DEEP);
    _canvas.drawRoundRect(cx - hw, cy - hh, hw * 2, hh * 2, 6, mInk);
    for (int i = 0; i < 6; i++) {
      int x = cx - hw + 6 + i * ((hw * 2 - 12) / 5);
      _canvas.fillRoundRect(x, cy - 4, 8, 8, 2, mInk);
    }
  }

  void drawOMouth() {
    const uint16_t mInk = accentInk();
    _canvas.drawCircle(CXC, 154, 16, INK_GLOW);
    _canvas.drawCircle(CXC, 154, 14, mInk);
    _canvas.drawCircle(CXC, 154, 13, mInk);
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

  void drawEyeSide(bool left, const Preset &base, float blink, int yOff = 0) {
    int cx = left ? EYE_L : EYE_R;
    int cy = EYE_Y + yOff;
    int r = base.eyeR;
    bool wink = (left && base.winkLeft) || (!left && base.winkRight);
    uint16_t tint = base.eyeTint ? base.eyeTint : EYE;

    if (wink) {
      _canvas.fillCircle(cx, cy, r + 2, EYE_GLOW);
      _canvas.fillCircle(cx, cy, r, tint);
      drawWinkLine(cx, cy, r);
      return;
    }

    _canvas.fillCircle(cx, cy, r + 2, EYE_GLOW);
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

  // Texto del título dentro del sprite, centrado arriba, sin fondo (transparente).
  // Layout precalculado en setTopTitle() — aquí SOLO imprime (barato por frame).
  void drawTopTitle() {
    if (_topTitle.length() == 0) return;
    _canvas.setFont(&fonts::DejaVu12);
    _canvas.setTextWrap(false);
    _canvas.setTextColor(TFT_CYAN);
    _canvas.setCursor(_topTitleX, 2);
    _canvas.print(_topTitle);
  }

  void draw() {
    if (_emotion == Emotion::Vibing) {
      drawVibingFace();
      drawTopTitle();
      pushFaceSprite();
      if (_showGear) drawGearOnScreen();
      return;
    }

    Preset p = preset();
    _canvas.fillSprite(BG);

    float t = millis() * 0.001f;
    float browDy = sinf(t * 4.0f + _browPhase) * 0.8f;
    float browTilt = sinf(t * 3.0f + _browPhase * 0.7f) * 0.6f;
    if (_talking) {
      // Talking expression: the brows lift on loud syllables (negative = up) with a quick
      // flutter and a little tilt, so the eyes "talk" along instead of staying frozen.
      float a = _mouthAmpSmooth / 100.0f;
      browDy -= a * 4.0f + sinf(t * 11.0f) * a * 1.3f;
      browTilt += sinf(t * 7.0f + _browPhase) * a * 1.6f;
    }
    {
      float lr = (millis() < _transitionEnd) ? 0.10f : 0.28f;
      _browDySmooth   += (browDy   - _browDySmooth)   * lr;
      _browTiltSmooth += (browTilt - _browTiltSmooth) * lr;
      browDy   = _browDySmooth;
      browTilt = _browTiltSmooth;
    }
    if (_listening) {
      browDy   = -3.5f + sinf(t * 4.8f) * 0.5f;
      browTilt = -0.18f + sinf(t * 3.1f) * 0.12f;
    }

    drawCapsule();

    _canvas.setClipRect(VISOR_CLIP_X, VISOR_CLIP_Y, VISOR_CLIP_W, VISOR_CLIP_H);
    float blink = _listening ? 0.0f : _blinkAmt;
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
    _canvas.clearClipRect();

    drawBrows(p, browDy, browTilt);

    if (p.tear) drawTear(EYE_L, EYE_Y, p.eyeR);

    if (p.mouthKind == MouthKind::Compact) drawCompactMouth();
    else if (p.mouthKind == MouthKind::O) drawOMouth();
    else drawMouthGrid(p.mouth);

    drawTopTitle();
    pushFaceSprite();
    if (_showGear) drawGearOnScreen();
  }
};

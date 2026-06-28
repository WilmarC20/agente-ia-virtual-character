// Análisis espectral ligero (Goertzel) para ecualizador vibing durante TTS/música.
#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

static constexpr int kPlaybackSpecBands = 12;

static inline float goertzelMagNorm(const int16_t *s, size_t n, float freq, float sr) {
  if (n < 8 || freq <= 0.0f) return 0.0f;
  const float w = 2.0f * 3.14159265f * freq / sr;
  const float coeff = 2.0f * cosf(w);
  float s1 = 0.0f, s2 = 0.0f;
  for (size_t i = 0; i < n; i++) {
    const float s0 = (float)s[i] + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  const float p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
  return sqrtf(p > 0.0f ? p : 0.0f) / (float)n;
}

// Bajos → agudos (voz TTS / música).
static constexpr float kBandFreqHz[kPlaybackSpecBands] = {
  80.0f, 120.0f, 180.0f, 260.0f, 380.0f, 550.0f,
  780.0f, 1100.0f, 1600.0f, 2300.0f, 3400.0f, 5200.0f
};

// Ganancia por banda: bajos y brillos un poco más visibles.
static constexpr float kBandGain[kPlaybackSpecBands] = {
  1.45f, 1.35f, 1.25f, 1.15f, 1.08f, 1.05f,
  1.02f, 1.06f, 1.12f, 1.22f, 1.35f, 1.50f
};

inline void computePlaybackSpectrum(const int16_t *samples, size_t nSamples, float sr,
                                    float outBands[kPlaybackSpecBands], float *beatOut) {
  if (beatOut) *beatOut = 1.0f;
  if (!samples || nSamples < 16 || !outBands) {
    if (outBands) memset(outBands, 0, sizeof(float) * kPlaybackSpecBands);
    return;
  }

  float bassRaw = 0.0f;
  for (int b = 0; b < kPlaybackSpecBands; b++) {
    const float mag = goertzelMagNorm(samples, nSamples, kBandFreqHz[b], sr);
    float norm = mag * kBandGain[b] * 38.0f;
    if (b < 3) bassRaw += norm;
    norm = powf(norm, 0.28f);
    if (norm > 1.0f) norm = 1.0f;
    outBands[b] = norm * 220.0f;
  }

  if (beatOut) {
    static float bassEnv = 0.0f;
    bassEnv = fmaxf(bassRaw * 0.34f, bassEnv * 0.86f);
    if (bassRaw > bassEnv * 1.08f && bassRaw > 0.04f) *beatOut = 1.55f;
  }
}

inline void computeMicStyleBands(const int16_t *samples, size_t nSamples, float sr,
                                 uint8_t *bands, int nBands) {
  if (!bands || nBands < 1) return;
  float tmp[kPlaybackSpecBands];
  computePlaybackSpectrum(samples, nSamples, sr, tmp, nullptr);
  const int nb = nBands > kPlaybackSpecBands ? kPlaybackSpecBands : nBands;
  for (int b = 0; b < nb; b++) {
    int lvl = (int)(tmp[b] * (255.0f / 220.0f) + 0.5f);
    if (lvl > 255) lvl = 255;
    if (lvl < 0) lvl = 0;
    bands[b] = (uint8_t)lvl;
  }
  for (int b = nb; b < nBands; b++) bands[b] = 0;
}

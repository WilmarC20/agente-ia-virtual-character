#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <ctype.h>

struct TouchCalib {
  uint8_t swapXY = 0;
  uint8_t invertX = 0;
  uint8_t invertY = 0;
  uint8_t showDebug = 0;
};

inline TouchCalib g_touchActiveCalib;
inline char g_touchActivePres[20] = "bender";

inline TouchCalib touchCalibDefaults(const char *presentationId) {
  TouchCalib c{};
  if (presentationId && strcasecmp(presentationId, "kitt") == 0) {
    c.invertX = 1;
    c.invertY = 1;
    c.showDebug = 0;
    return c;
  }
  // Bender landscape (equivale al mapeo rot=1 histórico).
  c.swapXY = 1;
  c.invertX = 1;
  c.invertY = 0;
  return c;
}

inline void touchCalibSanitizeId(const char *in, char *out, size_t outLen) {
  if (!out || outLen == 0) return;
  out[0] = '\0';
  if (!in || !in[0]) {
    strncpy(out, "bender", outLen - 1);
    out[outLen - 1] = '\0';
    return;
  }
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 1 < outLen; i++) {
    const char ch = in[i];
    if (isalnum((unsigned char)ch) || ch == '_' || ch == '-') {
      out[j++] = (char)tolower((unsigned char)ch);
    }
  }
  out[j] = '\0';
  if (j == 0) strncpy(out, "bender", outLen - 1);
}

inline TouchCalib touchCalibFromPacked(uint32_t packed, const char *presentationId) {
  TouchCalib c = touchCalibDefaults(presentationId);
  if (packed == 0xFFFFFFFFu) return c;
  c.swapXY = packed & 1;
  c.invertX = (packed >> 1) & 1;
  c.invertY = (packed >> 2) & 1;
  c.showDebug = (packed >> 3) & 1;
  return c;
}

inline uint32_t touchCalibPack(const TouchCalib &c) {
  return (uint32_t)(c.swapXY & 1) | ((uint32_t)(c.invertX & 1) << 1) |
         ((uint32_t)(c.invertY & 1) << 2) | ((uint32_t)(c.showDebug & 1) << 3);
}

inline TouchCalib touchCalibLoadFromNvs(const char *presentationId) {
  char id[20];
  touchCalibSanitizeId(presentationId, id, sizeof(id));
  Preferences p;
  p.begin("touchc", true);
  const uint32_t packed = p.getUInt(id, 0xFFFFFFFFu);
  p.end();
  return touchCalibFromPacked(packed, id);
}

inline void touchCalibSaveToNvs(const char *presentationId, const TouchCalib &c) {
  char id[20];
  touchCalibSanitizeId(presentationId, id, sizeof(id));
  Preferences p;
  p.begin("touchc", false);
  p.putUInt(id, touchCalibPack(c));
  p.end();
}

inline void touchCalibActivate(const char *presentationId) {
  char id[20];
  touchCalibSanitizeId(presentationId, id, sizeof(id));
  strncpy(g_touchActivePres, id, sizeof(g_touchActivePres) - 1);
  g_touchActivePres[sizeof(g_touchActivePres) - 1] = '\0';
  g_touchActiveCalib = touchCalibLoadFromNvs(id);
  Serial.printf("touch calib [%s] swap=%u invX=%u invY=%u dbg=%u\n", id,
                g_touchActiveCalib.swapXY, g_touchActiveCalib.invertX,
                g_touchActiveCalib.invertY, g_touchActiveCalib.showDebug);
}

inline const TouchCalib &touchCalibActive() { return g_touchActiveCalib; }
inline const char *touchCalibActivePresentation() { return g_touchActivePres; }

// Modo calibración: overlay de coords; el toque no debe activar micrófono ni gestos.
inline bool touchCalibMode() { return touchCalibActive().showDebug != 0; }

inline void touchCalibApplyRuntime(const char *presentationId, const TouchCalib &c) {
  char id[20];
  touchCalibSanitizeId(presentationId, id, sizeof(id));
  touchCalibSaveToNvs(id, c);
  if (strcasecmp(id, g_touchActivePres) == 0) g_touchActiveCalib = c;
}

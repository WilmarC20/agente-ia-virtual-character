#pragma once
// Typewriter caption — sync con TTS mouthAnim (40 ms/glyph)
#include <Arduino.h>

struct SpeechCaptionState {
  uint8_t mode = 0;      // 0 hidden | 1 karaoke | 2 ascii
  uint16_t charMs = 40;
  size_t visible = 0;
  String full;
};

inline void scBegin(SpeechCaptionState &st, const String &text) {
  st.full = text;
  st.visible = 0;
}

inline bool scTick(SpeechCaptionState &st, uint32_t nowMs, uint32_t &lastMs) {
  if (st.mode == 0 || st.visible >= st.full.length()) return false;
  if (nowMs - lastMs < st.charMs) return false;
  lastMs = nowMs;
  st.visible++;
  return true;
}

inline String scVisible(const SpeechCaptionState &st) {
  return st.full.substring(0, st.visible);
}

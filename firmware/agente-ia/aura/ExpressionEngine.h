#pragma once

#include <Arduino.h>

class Face;

struct EmotionStatePacket {
  const char *emotion = "neutral";
  float intensity = 0.7f;
  uint32_t recoveryMs = 8000;
};

class ExpressionEngine {
 public:
  void bind(Face *face) { _face = face; }
  void apply(const EmotionStatePacket &state);
  const EmotionStatePacket &last() const { return _last; }

 private:
  Face *_face = nullptr;
  EmotionStatePacket _last;
};

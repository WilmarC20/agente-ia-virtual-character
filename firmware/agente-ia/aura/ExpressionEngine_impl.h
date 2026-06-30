#pragma once

#include "ExpressionEngine.h"
#include "../face.h"

inline void ExpressionEngine::apply(const EmotionStatePacket &state) {
  _last = state;
  if (!_face) return;
  _face->setEmotion(emotionFromString(String(state.emotion)), state.intensity);
}

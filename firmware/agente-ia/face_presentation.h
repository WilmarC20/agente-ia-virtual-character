// Presentación visual por personaje (cara Bender vs tablero KITT).
#pragma once

#include <Arduino.h>

enum class FacePresentation : uint8_t { Bender = 0, Kitt = 1 };

inline FacePresentation facePresentationFromId(const char *id) {
  if (!id || !id[0]) return FacePresentation::Bender;
  if (strcasecmp(id, "kitt") == 0) return FacePresentation::Kitt;
  return FacePresentation::Bender;
}

inline const char *facePresentationId(FacePresentation p) {
  return p == FacePresentation::Kitt ? "kitt" : "bender";
}

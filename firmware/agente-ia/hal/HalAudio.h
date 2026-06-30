#pragma once

#include "../face.h"

class HalAudio {
 public:
  void bind(Face *face) { _face = face; }

  bool isTalking() const { return _face && _face->isTalking(); }
  bool isListening() const { return _face && _face->isListening(); }
  float mouthAmp() const { return _face ? _face->mouthAmpSmooth() : 0.0f; }

 private:
  Face *_face = nullptr;
};

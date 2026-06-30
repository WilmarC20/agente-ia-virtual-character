#pragma once

class Face;

class EmotionFaceWidget {
 public:
  void bind(Face *face) { _face = face; }
  void draw();

 private:
  Face *_face = nullptr;
};

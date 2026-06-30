#pragma once

// Fase 2: consume EmotionState del cerebro y actualiza widgets/face.
class ExpressionEngine {
 public:
  void setEmotion(const char *name, float intensity) {
    (void)name;
    _intensity = intensity;
  }
  float intensity() const { return _intensity; }

 private:
  float _intensity = 0.5f;
};

#pragma once

#include "../config.h"

// HAL RGB — stub sin librería externa (WS2812 se cableará con RMT en iteración futura).
class HalRgbLed {
 public:
  void begin() {
#if ENABLE_RGB_LED
    pinMode(PIN_RGB_LED, OUTPUT);
    digitalWrite(PIN_RGB_LED, LOW);
#endif
  }

  void setEmotionColor(uint8_t r, uint8_t g, uint8_t b) {
    (void)r;
    (void)g;
    (void)b;
  }
};

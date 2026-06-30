#pragma once

#include "../config.h"

class HalSensor {
 public:
  void begin() { pinMode(PIN_BAT_ADC, INPUT); }

  int batteryRaw() const { return analogRead(PIN_BAT_ADC); }

  float batteryVolts() const {
    const int raw = batteryRaw();
    return (raw / 4095.0f) * 3.3f * 2.0f;
  }
};

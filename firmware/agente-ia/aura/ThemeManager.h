#pragma once

#include <stdint.h>
#include <string.h>

// Paleta del tema activo (Fase 1: KITT embebido; futuro: SPIFFS/SD).
struct AuraTheme {
  uint16_t background = 0x0000;
  uint16_t red = 0xF800;
  uint16_t orange = 0xFC60;
  uint16_t yellow = 0xFFE0;
  uint16_t blue = 0x041F;
  uint16_t pillOrange = 0xFB00;
  uint16_t segmentOff = 0x0000;
  uint16_t labelFg = 0x0000;
};

class ThemeManager {
 public:
  void loadKittDefaults() {
    _theme = AuraTheme{};
  }

  const AuraTheme &theme() const { return _theme; }
  uint16_t color(const char *name) const {
    if (!name) return _theme.background;
    if (strcmp(name, "red") == 0) return _theme.red;
    if (strcmp(name, "orange") == 0) return _theme.orange;
    if (strcmp(name, "yellow") == 0) return _theme.yellow;
    if (strcmp(name, "blue") == 0) return _theme.blue;
    if (strcmp(name, "pillOrange") == 0) return _theme.pillOrange;
    return _theme.background;
  }

 private:
  AuraTheme _theme;
};

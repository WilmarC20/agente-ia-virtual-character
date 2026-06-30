#pragma once

#include <stdint.h>
#include <string.h>
#include <ArduinoJson.h>

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
  void loadKittDefaults() { _theme = AuraTheme{}; }

  static uint16_t parseHexColor(const char *hex) {
    if (!hex || hex[0] != '#') return 0;
    const unsigned long v = strtoul(hex + 1, nullptr, 16);
    const uint8_t r = (v >> 16) & 0xFF;
    const uint8_t g = (v >> 8) & 0xFF;
    const uint8_t b = v & 0xFF;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  }

  bool loadColorsFromJson(const char *json, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len)) return false;
    if (!doc["background"].isNull()) _theme.background = parseHexColor(doc["background"]);
    if (!doc["red"].isNull()) _theme.red = parseHexColor(doc["red"]);
    if (!doc["orange"].isNull()) _theme.orange = parseHexColor(doc["orange"]);
    if (!doc["yellow"].isNull()) _theme.yellow = parseHexColor(doc["yellow"]);
    if (!doc["blue"].isNull()) _theme.blue = parseHexColor(doc["blue"]);
    if (!doc["pillOrange"].isNull()) _theme.pillOrange = parseHexColor(doc["pillOrange"]);
    if (!doc["segmentOff"].isNull()) _theme.segmentOff = parseHexColor(doc["segmentOff"]);
    if (!doc["labelFg"].isNull()) _theme.labelFg = parseHexColor(doc["labelFg"]);
    return true;
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

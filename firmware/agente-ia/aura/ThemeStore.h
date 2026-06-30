#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include "config.h"
#include "ThemeManager.h"

// Cache de temas en SPIFFS (/aura/themes/<id>/).
class ThemeStore {
 public:
  bool begin() {
    if (_ready) return true;
    _ready = SPIFFS.begin(false, SPIFFS_PARTITION_LABEL);
    return _ready;
  }

  bool saveFile(const char *themeId, const char *filename, const uint8_t *data, size_t len) {
    if (!begin() || !themeId || !filename || !data) return false;
    String path = String("/aura/") + themeId + "/" + filename;
    File f = SPIFFS.open(path, FILE_WRITE);
    if (!f) return false;
    const size_t n = f.write(data, len);
    f.close();
    return n == len;
  }

  bool loadIntoTheme(const char *themeId, ThemeManager &tm) {
    if (!begin() || !themeId) return false;
    String path = String("/aura/") + themeId + "/colors.json";
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) return false;
    String body = f.readString();
    f.close();
    return tm.loadColorsFromJson(body.c_str(), body.length());
  }

 private:
  bool _ready = false;
};

inline ThemeStore g_themeStore;

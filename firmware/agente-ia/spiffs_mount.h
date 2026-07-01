#pragma once

#include <SPIFFS.h>
#include <esp_spiffs.h>
#include "config.h"

// Montaje único de la partición "spiffs" en /spiffs (WAV, atlas KITT, temas AURA).
inline bool ensureProjectSpiffs() {
  const char *label = SPIFFS_PARTITION_LABEL;
  if (esp_spiffs_mounted(label)) return true;
  SPIFFS.end();
  return SPIFFS.begin(false, "/spiffs", 10, label);
}

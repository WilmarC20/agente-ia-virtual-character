// Short WAV clips from SPIFFS (16 kHz, mono, 16-bit PCM).
#pragma once

#include <FS.h>
#include <SPIFFS.h>
#include <ESP_I2S.h>
#include <esp_partition.h>
#include "config.h"
#include "audio_output.h"

extern "C" {
#include <esp_spiffs.h>
}

extern I2SClass i2s;

static bool g_soundFxReady = false;

inline bool soundFxReady() { return g_soundFxReady; }

inline bool initSoundFx() {
  g_soundFxReady = false;
  const char *label = SPIFFS_PARTITION_LABEL;

  const esp_partition_t *part = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, label);
  if (!part) {
    Serial.printf(
      "Partition '%s' not in flash table.\n"
      "Tools -> Flash Size: 16MB, Partition: ESP SR 16M\n",
      label);
    return false;
  }
  Serial.printf("Partition '%s' @ 0x%x size %u KB\n", label, part->address, part->size / 1024);

  if (esp_spiffs_mounted(label)) {
    SPIFFS.end();
  }

  // Never format 7 MB on boot — esp_spiffs_format() blocks minutes with no Serial output.
  // Upload WAVs with upload_spiffs.ps1 instead.
  esp_vfs_spiffs_conf_t conf = {};
  conf.base_path = "/spiffs";
  conf.partition_label = label;
  conf.max_files = 10;
  conf.format_if_mount_failed = false;

  esp_err_t err = esp_vfs_spiffs_register(&conf);
  if (err != ESP_OK) {
    Serial.printf(
      "SPIFFS mount failed: %s (%d)\n"
      "  -> Reflashear imagen: firmware/agente-ia/upload_spiffs.ps1\n"
      "  (efectos desactivados; el resto sigue)\n",
      esp_err_to_name(err), err);
    return false;
  }

  size_t total = 0, used = 0;
  err = esp_spiffs_info(label, &total, &used);
  if (err != ESP_OK) {
    Serial.printf("SPIFFS info failed: %s\n", esp_err_to_name(err));
    esp_vfs_spiffs_unregister(label);
    return false;
  }

  g_soundFxReady = true;
  Serial.printf("SPIFFS OK (%u / %u bytes)\n", used, total);
  return true;
}

inline bool playSpiffsWav(const char *path) {
  if (!g_soundFxReady || !path || !path[0]) return false;
  File f = SPIFFS.open(path, "r");
  if (!f) {
    Serial.printf("SPIFFS missing: %s\n", path);
    return false;
  }
  if (f.size() <= 44) {
    f.close();
    return false;
  }
  f.seek(44);

  static uint8_t buf[1024];
  size_t pcmWritten = 0;
  preparePlayback(i2s);
  digitalWrite(PIN_SPK_EN, LOW);
  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    if (n == 0) break;
    pcmWritten += playMonoPcm16(i2s, buf, n);
  }
  delay(30);
  digitalWrite(PIN_SPK_EN, HIGH);
  f.close();
  endPlayback(i2s);

  Serial.printf("SFX played %s (%u bytes PCM)\n", path, pcmWritten);
  return pcmWritten > 0;
}

inline bool playSoundEffect(const char *fx) {
  if (!g_soundFxReady || !fx || !fx[0] || strcmp(fx, "none") == 0) return false;

  char path[32];
  snprintf(path, sizeof(path), "/%s.wav", fx);
  return playSpiffsWav(path);
}

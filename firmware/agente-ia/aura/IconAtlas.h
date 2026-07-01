#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include <LovyanGFX.hpp>
#include <esp_spiffs.h>
#include "../config.h"
#include "../spiffs_mount.h"
#include "../secrets.h"
#include "LayoutManager.h"

static constexpr int KITT_ATLAS_W = 240;
static constexpr int KITT_ATLAS_H = 320;
static constexpr size_t KITT_ATLAS_BYTES = (size_t)KITT_ATLAS_W * KITT_ATLAS_H * 2;

// Atlas RGB565 del tablero KITT (SPIFFS). Modulador central animado encima.
class KittIconAtlas {
 public:
  bool ready() const { return _ram != nullptr; }

  bool begin() {
    if (_ram) return true;
    if (!ensureProjectSpiffs()) {
      logOnce("KITT atlas: SPIFFS no montado");
      return false;
    }

    static const char *kPaths[] = {
        "/kitt_labels_atlas.bin",
        "/aura/kitt/labels_atlas.bin",
    };
    for (const char *path : kPaths) {
      File f = SPIFFS.open(path, FILE_READ);
      if (!f) continue;
      const size_t sz = f.size();
      if (sz < KITT_ATLAS_BYTES) {
        Serial.printf("KITT atlas: %s size=%u (need %u)\n", path, (unsigned)sz, (unsigned)KITT_ATLAS_BYTES);
        f.close();
        continue;
      }
      _ram = (uint16_t *)heap_caps_malloc(KITT_ATLAS_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!_ram) {
        logOnce("KITT atlas: sin PSRAM para buffer");
        f.close();
        return false;
      }
      const size_t n = f.read((uint8_t *)_ram, KITT_ATLAS_BYTES);
      f.close();
      if (n != KITT_ATLAS_BYTES) {
        free(_ram);
        _ram = nullptr;
        continue;
      }
      Serial.printf("KITT atlas: OK desde %s (%u bytes PSRAM)\n", path, (unsigned)n);
      return true;
    }
    logOnce("KITT atlas: archivo no encontrado en SPIFFS");
    File root = SPIFFS.open("/");
    if (root && root.isDirectory()) {
      File f = root.openNextFile();
      while (f) {
        Serial.printf("  SPIFFS: %s (%u)\n", f.name(), (unsigned)f.size());
        f = root.openNextFile();
      }
    }
    return false;
  }

#if USE_AURA_THEME_SYNC
  bool fetchFromServer() {
    if (!ensureProjectSpiffs()) return false;
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    http.setTimeout(20000);
    String url = String(BRAIN_SERVER_URL) + "/api/themes/kitt/labels_atlas.bin";
    if (!http.begin(url)) return false;
    http.addHeader("X-Device-MAC", WiFi.macAddress());
    const int code = http.GET();
    if (code != 200) {
      http.end();
      Serial.printf("KITT atlas: HTTP %d\n", code);
      return false;
    }

    File f = SPIFFS.open("/kitt_labels_atlas.bin", FILE_WRITE);
    if (!f) {
      http.end();
      return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    uint8_t chunk[1024];
    size_t total = 0;
    while (http.connected() && total < KITT_ATLAS_BYTES + 4096) {
      const size_t avail = stream->available();
      if (avail == 0) {
        if (!http.connected()) break;
        delay(1);
        continue;
      }
      const size_t n = stream->readBytes(chunk, min(avail, sizeof(chunk)));
      if (n == 0) break;
      f.write(chunk, n);
      total += n;
    }
    f.close();
    http.end();

    if (_ram) {
      free(_ram);
      _ram = nullptr;
    }
    Serial.printf("KITT atlas: descargado %u bytes\n", (unsigned)total);
    return total >= KITT_ATLAS_BYTES && begin();
  }
#endif

  void drawDashboard(lgfx::LGFX_Sprite &c) {
    if (!begin()) return;
    // Atlas en RGB565 LE (atlas_builder); uint16_t* sin tipo se interpreta como swap565.
    c.pushImage(0, 0, KITT_ATLAS_W, KITT_ATLAS_H,
                reinterpret_cast<const lgfx::rgb565_t *>(_ram));
  }

  void clearModulatorZone(lgfx::LGFX_Sprite &c, const LayoutManager &layout) {
    AuraRect z = layout.get("modulator");
    if (z.valid) c.fillRect(z.x, z.y, z.w, z.h, 0x0000);
  }

 private:
  uint16_t *_ram = nullptr;
  bool _logged = false;

  void logOnce(const char *msg) {
    if (_logged) return;
    _logged = true;
    Serial.println(msg);
  }
};

inline KittIconAtlas g_kittAtlas;

inline void auraLoadKittAtlas() {
#if USE_KITT_SPRITE_ATLAS
  if (g_kittAtlas.ready()) return;
  if (g_kittAtlas.begin()) return;
#if USE_AURA_THEME_SYNC
  if (WiFi.status() == WL_CONNECTED) g_kittAtlas.fetchFromServer();
#endif
#endif
}

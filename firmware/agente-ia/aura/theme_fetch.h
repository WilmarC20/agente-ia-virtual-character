#pragma once

#include <WiFi.h>
#include <HTTPClient.h>
#include "config.h"
#include "secrets.h"

#if USE_AURA_THEME_SYNC

inline String fetchThemeFile(const char *themeId, const char *filename) {
  if (!themeId || !filename || WiFi.status() != WL_CONNECTED) return "";
  HTTPClient http;
  http.setTimeout(8000);
  String url = String(BRAIN_SERVER_URL) + "/api/themes/" + themeId + "/" + filename;
  if (!http.begin(url)) return "";
  http.addHeader("X-Device-MAC", WiFi.macAddress());
  const int code = http.GET();
  if (code != 200) {
    http.end();
    return "";
  }
  const String body = http.getString();
  http.end();
  return body;
}

inline bool fetchThemeBinary(const char *themeId, const char *filename,
                             uint8_t *out, size_t outCap, size_t *outLen) {
  if (!themeId || !filename || !out || !outLen || WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.setTimeout(15000);
  String url = String(BRAIN_SERVER_URL) + "/api/themes/" + themeId + "/" + filename;
  if (!http.begin(url)) return false;
  http.addHeader("X-Device-MAC", WiFi.macAddress());
  const int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  size_t total = 0;
  while (http.connected() && total < outCap) {
    const size_t avail = stream->available();
    if (avail == 0) {
      delay(1);
      continue;
    }
    const size_t n = stream->readBytes(out + total, min(avail, outCap - total));
    total += n;
    if (total >= (size_t)http.getSize()) break;
  }
  http.end();
  *outLen = total;
  return total > 0;
}

#else

inline String fetchThemeFile(const char *, const char *) { return ""; }

inline bool fetchThemeBinary(const char *, const char *, uint8_t *, size_t, size_t *) {
  return false;
}

#endif

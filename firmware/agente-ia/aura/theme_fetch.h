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

#else

inline String fetchThemeFile(const char *, const char *) { return ""; }

#endif

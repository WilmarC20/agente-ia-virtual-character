#pragma once

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "secrets.h"
#include "brain_ws_client.h"

// Transporte cerebro ↔ dispositivo: WebSocket persistente con fallback HTTP.
class BrainTransport {
 public:
  using CommandHandler = void (*)(JsonObjectConst cmd, JsonVariantConst root);

  void setHandler(CommandHandler fn) { _handler = fn; }

  void pumpWs() {
#if ENABLE_DEVICE_WS
    brainWsClient().pump();
#endif
  }

  // NO bloqueante: mantiene un poll_wait en vuelo sobre el WS persistente y
  // despacha la respuesta cuando llega (vía pump()). El loop sigue muestreando
  // el touch en cada iteración — sin esto el long-poll congelaba la pantalla.
  bool poll(CommandHandler fn = nullptr) {
    if (WiFi.status() != WL_CONNECTED) return false;
    CommandHandler h = fn ? fn : _handler;
    if (!h) return false;

#if ENABLE_DEVICE_WS
    return brainWsClient().pollNonBlocking(h);
#elif ENABLE_DEVICE_LONG_POLL
    // Fallback HTTP: poll corto y con throttle, nunca el long-poll bloqueante.
    static uint32_t lastPoll = 0;
    const uint32_t now = millis();
    if (now - lastPoll < 1500) return false;
    lastPoll = now;
    return pollOnce(h);
#else
    static uint32_t lastPoll = 0;
    const uint32_t now = millis();
    if (now - lastPoll < 2000) return false;
    lastPoll = now;
    return pollOnce(h);
#endif
  }

 private:
  CommandHandler _handler = nullptr;

  static void addHeaders(HTTPClient &http) {
    http.addHeader("X-Device-MAC", WiFi.macAddress());
  }

  bool pollOnce(CommandHandler h) {
    HTTPClient http;
    http.setTimeout(4000);
    if (!http.begin(String(BRAIN_SERVER_URL) + "/api/dev/poll")) return false;
    addHeaders(http);
    const int code = http.GET();
    if (code != 200) {
      http.end();
      return false;
    }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err) return false;
    const bool got = !doc["cmd"].isNull();
    dispatch(doc, h);
    return got;
  }

  bool pollWait(CommandHandler h, int timeoutSec) {
    HTTPClient http;
    http.setTimeout((uint16_t)(timeoutSec * 1000 + 2000));
    String url = String(BRAIN_SERVER_URL) + "/api/dev/poll-wait?timeout=" + String(timeoutSec);
    if (!http.begin(url)) return false;
    addHeaders(http);
    const int code = http.GET();
    if (code != 200) {
      http.end();
      return false;
    }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err) return false;
    const bool got = !doc["cmd"].isNull();
    dispatch(doc, h);
    return got;
  }

  void dispatch(JsonDocument &doc, CommandHandler h) {
    JsonObjectConst cmd = doc["cmd"].isNull() ? JsonObjectConst() : doc["cmd"].as<JsonObjectConst>();
    h(cmd, doc.as<JsonVariantConst>());
  }

};

inline BrainTransport g_brainTransport;

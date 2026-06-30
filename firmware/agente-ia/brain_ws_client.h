#pragma once

#include <WiFi.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "secrets.h"

// Cliente WebSocket mínimo (RFC6455) — sin librería externa.
class BrainWsClient {
 public:
  using CommandHandler = void (*)(JsonObjectConst cmd, JsonVariantConst root);

  bool pollWait(CommandHandler h, int timeoutSec) {
#if !ENABLE_DEVICE_WS
    (void)h;
    (void)timeoutSec;
    return false;
#else
    if (WiFi.status() != WL_CONNECTED || !h) return false;
    if (!handshake()) return false;

    String req = String("{\"op\":\"poll_wait\",\"timeout\":") + String(timeoutSec) + "}";
    if (!wsSendText(req.c_str())) {
      disconnect();
      return false;
    }

    String body;
    const uint32_t waitMs = (uint32_t)(timeoutSec * 1000 + 4000);
    if (!wsReadText(body, waitMs)) {
      disconnect();
      return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, body)) {
      disconnect();
      return false;
    }
    JsonObjectConst cmd = doc["cmd"].isNull() ? JsonObjectConst() : doc["cmd"].as<JsonObjectConst>();
    h(cmd, doc.as<JsonVariantConst>());
    return !doc["cmd"].isNull();
#endif
  }

 private:
  WiFiClient _client;
  bool _handshaken = false;
  String _host;
  uint16_t _port = 8000;
  String _path = "/ws/device";

  bool parseBrainUrl() {
    String url = BRAIN_SERVER_URL;
    if (url.startsWith("http://")) url = url.substring(7);
    else if (url.startsWith("https://")) url = url.substring(8);
    const int slash = url.indexOf('/');
    const String hostPort = slash >= 0 ? url.substring(0, slash) : url;
    if (slash >= 0) _path = url.substring(slash);
    const int colon = hostPort.indexOf(':');
    if (colon >= 0) {
      _host = hostPort.substring(0, colon);
      _port = (uint16_t)hostPort.substring(colon + 1).toInt();
    } else {
      _host = hostPort;
      _port = 80;
    }
    return _host.length() > 0;
  }

  static String randomKey() {
    char k[17];
    for (int i = 0; i < 16; i++) k[i] = "abcdefghijklmnopqrstuvwxyz0123456789"[random(36)];
    k[16] = 0;
    return String(k);
  }

  void disconnect() {
    _handshaken = false;
    _client.stop();
  }

  bool handshake() {
    if (_client.connected() && _handshaken) return true;
    disconnect();
    if (!parseBrainUrl()) return false;
    if (!_client.connect(_host.c_str(), _port, 5000)) return false;

    const String key = randomKey();
    String req = String("GET ") + _path + " HTTP/1.1\r\n";
    req += "Host: " + _host + ":" + String(_port) + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + key + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    req += "X-Device-MAC: " + WiFi.macAddress() + "\r\n\r\n";
    _client.print(req);

    uint32_t t0 = millis();
    String headers;
    while (millis() - t0 < 6000) {
      while (_client.available()) {
        const char c = (char)_client.read();
        headers += c;
        if (headers.endsWith("\r\n\r\n")) {
          if (headers.indexOf("101") < 0) {
            disconnect();
            return false;
          }
          _handshaken = true;
          return true;
        }
      }
      delay(1);
    }
    disconnect();
    return false;
  }

  bool wsSendText(const char *payload) {
    if (!payload) return false;
    const size_t len = strlen(payload);
    uint8_t hdr[8];
    size_t hlen = 2;
    hdr[0] = 0x81;
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) mask[i] = (uint8_t)random(256);
    if (len < 126) {
      hdr[1] = 0x80 | (uint8_t)len;
    } else if (len < 65536) {
      hdr[1] = 0x80 | 126;
      hdr[2] = (uint8_t)((len >> 8) & 0xFF);
      hdr[3] = (uint8_t)(len & 0xFF);
      hlen = 4;
    } else {
      return false;
    }
    _client.write(hdr, hlen);
    _client.write(mask, 4);
    for (size_t i = 0; i < len; i++) {
      const uint8_t b = (uint8_t)payload[i] ^ mask[i % 4];
      _client.write(b);
    }
    return true;
  }

  bool wsReadText(String &out, uint32_t timeoutMs) {
    out = "";
    const uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
      if (!_client.connected()) return false;
      if (!_client.available()) {
        delay(1);
        continue;
      }
      const uint8_t b0 = (uint8_t)_client.read();
      const uint8_t opcode = b0 & 0x0F;
      const uint8_t b1 = (uint8_t)_client.read();
      const bool masked = b1 & 0x80;
      uint32_t plen = b1 & 0x7F;
      if (plen == 126) {
        plen = ((uint32_t)_client.read() << 8) | (uint32_t)_client.read();
      } else if (plen == 127) {
        return false;
      }
      uint8_t mask[4] = {0};
      if (masked) _client.readBytes(mask, 4);
      String chunk;
      chunk.reserve(plen + 1);
      for (uint32_t i = 0; i < plen; i++) {
        uint8_t c = (uint8_t)_client.read();
        if (masked) c ^= mask[i % 4];
        chunk += (char)c;
      }
      if (opcode == 0x8) {
        disconnect();
        return false;
      }
      if (opcode == 0x9) continue;
      if (opcode == 0x1 || opcode == 0x0) out += chunk;
      if ((b0 & 0x80) && out.length() > 0) return true;
    }
    return false;
  }
};

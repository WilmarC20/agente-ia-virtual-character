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

  bool isActive() { return _handshaken && _client.connected(); }

  // Drenar pings/cierres sin bloquear — llamar desde loop() y durante audio/TTS.
  // Una respuesta de texto (la del poll_wait) se guarda en _pendingReply para que
  // el loop la despache; así el muestreo táctil nunca se bloquea esperando comandos.
  void pump() {
#if ENABLE_DEVICE_WS
    if (!isActive()) return;
    uint8_t opcode = 0;
    static uint8_t payload[768];
    size_t len = 0;
    while (readFrame(opcode, payload, sizeof(payload), len, 0)) {
      if (opcode == 0x9) {
        sendPong(payload, len);
        continue;
      }
      if (opcode == 0x8) {
        disconnect();
        return;
      }
      if (opcode == 0x1 || opcode == 0x0) {  // respuesta del poll_wait
        _pendingReply = "";
        _pendingReply.reserve(len + 1);
        for (size_t i = 0; i < len; i++) _pendingReply += (char)payload[i];
        _hasPending = true;
        _pollOutstanding = false;
        continue;
      }
    }
#endif
  }

  // Poll de comandos NO bloqueante sobre el WS persistente. Mantiene un único
  // poll_wait en vuelo; el servidor responde al instante al encolar un comando,
  // y pump() captura la respuesta. Nunca bloquea el loop esperando comandos.
  bool pollNonBlocking(CommandHandler h) {
#if !ENABLE_DEVICE_WS
    (void)h;
    return false;
#else
    if (WiFi.status() != WL_CONNECTED || !h) return false;
    if (!isActive()) {
      if (millis() - _lastConnectTry < 5000) return false;  // no martillar si el server está caído
      _lastConnectTry = millis();
      if (!handshake()) return false;
      _pollOutstanding = false;
    }

    bool got = false;
    if (_hasPending) {
      _hasPending = false;
      JsonDocument doc;
      if (!deserializeJson(doc, _pendingReply)) {
        JsonObjectConst cmd = doc["cmd"].isNull() ? JsonObjectConst() : doc["cmd"].as<JsonObjectConst>();
        h(cmd, doc.as<JsonVariantConst>());
        got = !doc["cmd"].isNull();
      }
      _pendingReply = "";
    }

    if (!_pollOutstanding) {
      const String req = String("{\"op\":\"poll_wait\",\"timeout\":25}");
      if (wsSendText(req.c_str())) _pollOutstanding = true;
      else disconnect();
    }
    return got;
#endif
  }

  // true = transporte OK (aunque cmd sea null). gotCmd indica si había comando.
  bool pollWait(CommandHandler h, int timeoutSec, bool *gotCmd = nullptr) {
#if !ENABLE_DEVICE_WS
    (void)h;
    (void)timeoutSec;
    if (gotCmd) *gotCmd = false;
    return false;
#else
    if (gotCmd) *gotCmd = false;
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
    const bool hasCmd = !doc["cmd"].isNull();
    if (gotCmd) *gotCmd = hasCmd;
    return true;
#endif
  }

 private:
  WiFiClient _client;
  bool _handshaken = false;
  String _host;
  uint16_t _port = 8000;
  String _path = "/ws/device";
  bool _pollOutstanding = false;   // hay un poll_wait en vuelo esperando respuesta
  bool _hasPending = false;        // pump() capturó una respuesta sin despachar
  String _pendingReply;            // cuerpo JSON de la última respuesta del poll_wait
  uint32_t _lastConnectTry = 0;    // throttle de reconexión cuando el server está caído

  static size_t b64Encode(const uint8_t *in, size_t inLen, char *out, size_t outCap) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < inLen; i += 3) {
      uint32_t v = (uint32_t)in[i] << 16;
      if (i + 1 < inLen) v |= (uint32_t)in[i + 1] << 8;
      if (i + 2 < inLen) v |= (uint32_t)in[i + 2];
      if (o + 4 > outCap) return 0;
      out[o++] = tbl[(v >> 18) & 63];
      out[o++] = tbl[(v >> 12) & 63];
      out[o++] = (i + 1 < inLen) ? tbl[(v >> 6) & 63] : '=';
      out[o++] = (i + 2 < inLen) ? tbl[v & 63] : '=';
    }
    if (o < outCap) out[o] = 0;
    return o;
  }

  static String wsKeyBase64() {
    uint8_t nonce[16];
    for (int i = 0; i < 16; i++) nonce[i] = (uint8_t)esp_random();
    char buf[28];
    b64Encode(nonce, 16, buf, sizeof(buf));
    return String(buf);
  }

  bool parseBrainUrl() {
    String url = BRAIN_SERVER_URL;
    if (url.startsWith("http://")) url = url.substring(7);
    else if (url.startsWith("https://")) url = url.substring(8);
    const int slash = url.indexOf('/');
    const String hostPort = slash >= 0 ? url.substring(0, slash) : url;
    if (slash >= 0) {
      String p = url.substring(slash);
      _path = (p.length() <= 1) ? "/ws/device" : p;
    } else {
      _path = "/ws/device";
    }
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

  void disconnect() {
    _handshaken = false;
    _pollOutstanding = false;
    _hasPending = false;
    _pendingReply = "";
    _client.stop();
  }

  bool handshake() {
    if (_client.connected() && _handshaken) return true;
    disconnect();
    if (!parseBrainUrl()) return false;
    if (!_client.connect(_host.c_str(), _port, 5000)) return false;

    const String key = wsKeyBase64();
    String req = String("GET ") + _path + " HTTP/1.1\r\n";
    req += "Host: " + _host + ":" + String(_port) + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + key + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    req += "X-Device-MAC: " + WiFi.macAddress() + "\r\n\r\n";
    _client.write((const uint8_t *)req.c_str(), req.length());

    uint32_t t0 = millis();
    String headers;
    while (millis() - t0 < 6000) {
      pump();  // por si el servidor manda algo durante handshake
      while (_client.available()) {
        const char c = (char)_client.read();
        headers += c;
        if (headers.endsWith("\r\n\r\n")) {
          if (headers.indexOf(" 101 ") < 0 && headers.indexOf(" 101\r\n") < 0) {
            disconnect();
            return false;
          }
          _handshaken = true;
          String boot;
          wsReadText(boot, 1500);
          return true;
        }
      }
      delay(1);
    }
    disconnect();
    return false;
  }

  bool readFrame(uint8_t &opcode, uint8_t *payload, size_t payloadCap, size_t &outLen, uint32_t timeoutMs) {
    outLen = 0;
    if (!_client.connected()) return false;

    const uint32_t t0 = timeoutMs ? millis() : 0;
    while (!timeoutMs || (millis() - t0 < timeoutMs)) {
      if (_client.available() < 2) {
        if (!timeoutMs) return false;
        delay(1);
        continue;
      }

      const uint8_t b0 = (uint8_t)_client.read();
      opcode = b0 & 0x0F;
      const uint8_t b1 = (uint8_t)_client.read();
      const bool masked = b1 & 0x80;
      uint32_t plen = b1 & 0x7F;
      if (plen == 126) {
        while (_client.available() < 2) {
          if (timeoutMs && millis() - t0 >= timeoutMs) return false;
          delay(1);
        }
        plen = ((uint32_t)_client.read() << 8) | (uint32_t)_client.read();
      } else if (plen == 127) {
        return false;
      }

      uint8_t mask[4] = {0};
      if (masked) {
        while ((uint32_t)_client.available() < 4) {
          if (timeoutMs && millis() - t0 >= timeoutMs) return false;
          delay(1);
        }
        _client.readBytes(mask, 4);
      }

      outLen = plen > payloadCap ? payloadCap : plen;
      for (uint32_t i = 0; i < plen; i++) {
        while (!_client.available()) {
          if (timeoutMs && millis() - t0 >= timeoutMs) return false;
          delay(1);
        }
        uint8_t c = (uint8_t)_client.read();
        if (masked) c ^= mask[i % 4];
        if (i < outLen) payload[i] = c;
      }
      return true;
    }
    return false;
  }

  void sendPong(const uint8_t *payload, size_t len) {
    uint8_t hdr[10];
    size_t hlen = 2;
    hdr[0] = 0x8A;
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) mask[i] = (uint8_t)esp_random();
    if (len < 126) {
      hdr[1] = 0x80 | (uint8_t)len;
    } else if (len < 65536) {
      hdr[1] = 0x80 | 126;
      hdr[2] = (uint8_t)((len >> 8) & 0xFF);
      hdr[3] = (uint8_t)(len & 0xFF);
      hlen = 4;
    } else {
      return;
    }
    _client.write(hdr, hlen);
    _client.write(mask, 4);
    for (size_t i = 0; i < len; i++) {
      const uint8_t b = payload[i] ^ mask[i % 4];
      _client.write(b);
    }
  }

  bool wsSendText(const char *payload) {
    if (!payload) return false;
    const size_t len = strlen(payload);
    uint8_t hdr[8];
    size_t hlen = 2;
    hdr[0] = 0x81;
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) mask[i] = (uint8_t)esp_random();
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
      pump();
      uint8_t opcode = 0;
      uint8_t payload[512];
      size_t len = 0;
      if (!readFrame(opcode, payload, sizeof(payload), len, 50)) {
        delay(1);
        continue;
      }
      if (opcode == 0x8) {
        disconnect();
        return false;
      }
      if (opcode == 0x9) {
        sendPong(payload, len);
        continue;
      }
      if (opcode == 0x1 || opcode == 0x0) {
        for (size_t i = 0; i < len; i++) out += (char)payload[i];
        return true;
      }
    }
    return false;
  }
};

#if ENABLE_DEVICE_WS
inline BrainWsClient &brainWsClient() {
  static BrainWsClient inst;
  return inst;
}
#endif

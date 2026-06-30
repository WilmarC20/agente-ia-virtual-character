#pragma once

#include <Arduino.h>
#include <vector>

struct AuraEvent {
  String type;
  String payloadJson;
};

// Bus local en dispositivo (Fase 3). Hoy: cola en memoria, sin WebSocket.
class DeviceEventBus {
 public:
  void publish(const String &type, const String &payloadJson = "{}") {
    AuraEvent e{type, payloadJson};
    if (_queue.size() >= 32) _queue.erase(_queue.begin());
    _queue.push_back(e);
  }

  bool poll(AuraEvent &out) {
    if (_queue.empty()) return false;
    out = _queue.front();
    _queue.erase(_queue.begin());
    return true;
  }

 private:
  std::vector<AuraEvent> _queue;
};

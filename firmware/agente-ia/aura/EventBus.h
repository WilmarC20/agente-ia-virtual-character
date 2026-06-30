#pragma once

#include <Arduino.h>
#include <vector>
#include "../hal/DeviceEventBus.h"

// Bus AURA en dispositivo — reenvía eventos locales al HAL y escenas.
class AuraEventBus {
 public:
  void bind(DeviceEventBus *hal) { _hal = hal; }

  void publish(const char *type, const String &payloadJson = "{}") {
    if (_hal) _hal->publish(String(type), payloadJson);
    AuraEvent e;
    e.type = type;
    e.payloadJson = payloadJson;
    if (_subs.size() >= 24) _subs.erase(_subs.begin());
    _subs.push_back(e);
    for (auto &fn : _handlers) fn(e);
  }

  using Handler = void (*)(const AuraEvent &);
  void subscribe(Handler fn) { if (fn) _handlers.push_back(fn); }

  bool poll(AuraEvent &out) {
    if (_hal && _hal->poll(out)) return true;
    if (_subs.empty()) return false;
    out = _subs.front();
    _subs.erase(_subs.begin());
    return true;
  }

 private:
  DeviceEventBus *_hal = nullptr;
  std::vector<AuraEvent> _subs;
  std::vector<Handler> _handlers;
};

inline AuraEventBus g_auraBus;

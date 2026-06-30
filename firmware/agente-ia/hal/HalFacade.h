#pragma once

#include "HalDisplay.h"
#include "HalTouch.h"
#include "HalAudio.h"
#include "HalRgbLed.h"
#include "HalSensor.h"
#include "DeviceEventBus.h"

// Fachada HAL — punto único para drivers en Fase 3+.
class HalFacade {
 public:
  HalFacade(lgfx::LGFX_Device &gfx) : display(gfx), touch(gfx) {}

  HalDisplay display;
  HalTouch touch;
  HalAudio audio;
  HalRgbLed rgb;
  HalSensor sensor;
  DeviceEventBus events;
};

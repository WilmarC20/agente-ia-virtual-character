#pragma once

#include <LovyanGFX.hpp>

// Fase 3: envoltorio único sobre display/touch/audio. Hoy delega en drivers existentes.
class HalDisplay {
 public:
  explicit HalDisplay(lgfx::LGFX_Device &gfx) : _gfx(gfx) {}
  lgfx::LGFX_Device &gfx() { return _gfx; }

 private:
  lgfx::LGFX_Device &_gfx;
};

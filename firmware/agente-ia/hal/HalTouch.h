#pragma once

#include <LovyanGFX.hpp>
#include "../touch.h"

class HalTouch {
 public:
  HalTouch(lgfx::LGFX_Device &gfx) : _gfx(gfx) {}

  bool poll(int &x, int &y) {
    return touchReadPoint(_gfx.width(), _gfx.height(), x, y);
  }

  void waitRelease() { touchWaitRelease(_gfx.width(), _gfx.height()); }

 private:
  lgfx::LGFX_Device &_gfx;
};

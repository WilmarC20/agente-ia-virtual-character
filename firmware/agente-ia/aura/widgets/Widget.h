#pragma once

#include <LovyanGFX.hpp>
#include "../Renderer.h"
#include "../ThemeManager.h"
#include "../LayoutManager.h"

class Widget {
 public:
  virtual ~Widget() = default;
  virtual void draw(AuraRenderer &r, const ThemeManager &theme, const LayoutManager &layout) = 0;
};

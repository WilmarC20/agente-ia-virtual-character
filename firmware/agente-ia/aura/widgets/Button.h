#pragma once

#include "Widget.h"
#include "../Renderer.h"
#include "../ThemeManager.h"
#include "../LayoutManager.h"

class ButtonWidget : public Widget {
 public:
  ButtonWidget(const char *id, uint16_t color) : _id(id), _color(color) {}

  void draw(AuraRenderer &r, const ThemeManager &theme, const LayoutManager &layout) override {
    AuraRect rect = layout.get(_id);
    if (!rect.valid) return;
    r.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 6, _color ? _color : theme.theme().orange);
  }

 private:
  const char *_id;
  uint16_t _color;
};

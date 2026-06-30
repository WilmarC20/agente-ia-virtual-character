#pragma once

#include "../Widget.h"
#include "../Renderer.h"
#include "../ThemeManager.h"
#include "../LayoutManager.h"

// Widget de ejemplo para SDK — barra de progreso simple.
class GaugeWidget : public Widget {
 public:
  GaugeWidget(const char *regionId, float value01) : _regionId(regionId), _value(value01) {}

  void draw(AuraRenderer &r, const ThemeManager &theme, const LayoutManager &layout) override {
    AuraRect rect = layout.get(_regionId);
    if (!rect.valid) return;
    const uint16_t bg = theme.theme().segmentOff;
    const uint16_t fg = theme.theme().red;
    r.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 3, bg);
    const int fillW = (int)(rect.w * _value);
    if (fillW > 0) r.fillRoundRect(rect.x, rect.y, fillW, rect.h, 3, fg);
  }

 private:
  const char *_regionId;
  float _value;
};

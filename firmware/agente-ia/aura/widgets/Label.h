#pragma once

#include "Widget.h"
#include "../AtlasText.h"
#include "../Renderer.h"
#include "../ThemeManager.h"
#include "../LayoutManager.h"

class LabelWidget : public Widget {
 public:
  LabelWidget(const char *regionId, const char *text) : _regionId(regionId), _text(text) {}

  void draw(AuraRenderer &r, const ThemeManager &theme, const LayoutManager &layout) override {
    lgfx::LGFX_Sprite *c = r.sprite();
    if (!c) return;
    AuraRect rect = layout.get(_regionId);
    if (!rect.valid) return;
    AtlasText::drawCentered(*c, rect.x, rect.y, rect.w, rect.h, theme.theme().labelFg, theme.theme().background, _text);
  }

 private:
  const char *_regionId;
  const char *_text;
};

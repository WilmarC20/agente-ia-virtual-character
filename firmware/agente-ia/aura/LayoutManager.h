#pragma once

#include <stdint.h>

struct AuraRect {
  int x, y, w, h;
};

struct AuraLayout {
  int canvasW = 240;
  int canvasH = 320;
  int topX = 73, topW = 94, topH = 16, topPitch = 23, topY0 = 5, topRadius = 4;
  int ovalW = 44, ovalH = 26, ovalLX = 12, ovalRX = 184, ovalY0 = 118, ovalPitch = 38;
  int segW = 18, segH = 4, segGap = 2, colCenterY = 158;
  int colCxLeft = 90, colCxMid = 120, colCxRight = 150;
  int colSegSide = 15, colSegMid = 19;
  int botX = 73, botW = 94, botH = 24, botPitch = 28, botY0 = 230, botRadius = 8;
};

class LayoutManager {
 public:
  void loadKittDefaults() { _layout = AuraLayout{}; }
  const AuraLayout &layout() const { return _layout; }

  AuraRect topBar(int index) const {
    return {_layout.topX, _layout.topY0 + index * _layout.topPitch, _layout.topW, _layout.topH};
  }

  AuraRect ovalLeft(int index) const {
    return {_layout.ovalLX, _layout.ovalY0 + index * _layout.ovalPitch, _layout.ovalW, _layout.ovalH};
  }

  AuraRect ovalRight(int index) const {
    return {_layout.ovalRX, _layout.ovalY0 + index * _layout.ovalPitch, _layout.ovalW, _layout.ovalH};
  }

  AuraRect bottomBar(int index) const {
    return {_layout.botX, _layout.botY0 + index * _layout.botPitch, _layout.botW, _layout.botH};
  }

  AuraRect hitZoneSettingsP4() const { return ovalRight(3); }

 private:
  AuraLayout _layout;
};

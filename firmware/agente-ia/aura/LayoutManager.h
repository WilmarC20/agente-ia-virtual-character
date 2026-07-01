#pragma once

#include <stdint.h>
#include <string.h>

struct AuraRect {
  int x, y, w, h;
  bool valid = false;
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
  // Bender landscape 320x240
  int visorX = 40, visorY = 30, visorW = 240, visorH = 160;
  int mouthX = 120, mouthY = 155, mouthW = 80, mouthH = 28;
};

enum class KittButton : uint8_t {
  None = 0,
  Power,
  MinRpm,
  FuelOn,
  Ignitors,
  Air,
  Oil,
  P1,
  P2,
  S1,
  S2,
  P3,
  P4,
  AutoCruise,
  NormalCruise,
  Pursuit,
  Modulator,
};

class LayoutManager {
 public:
  enum class ThemeKind { Kitt, Bender };

  void loadKittDefaults() {
    _kind = ThemeKind::Kitt;
    _layout = AuraLayout{};
  }

  void loadBenderDefaults() {
    _kind = ThemeKind::Bender;
    _layout = AuraLayout{};
    _layout.canvasW = 320;
    _layout.canvasH = 240;
  }

  const AuraLayout &layout() const { return _layout; }
  ThemeKind kind() const { return _kind; }

  AuraRect get(const char *id) const {
    if (!id) return {};
    if (strcmp(id, "powerButton") == 0 || strcmp(id, "top.power") == 0) return topBar(0);
    if (strcmp(id, "settingsP4") == 0 || strcmp(id, "oval.p4") == 0) return hitZoneSettingsP4();
    if (strcmp(id, "modulator") == 0) return modulatorZone();
    if (strcmp(id, "visor") == 0) {
      return {_layout.visorX, _layout.visorY, _layout.visorW, _layout.visorH, true};
    }
    if (strcmp(id, "mouth") == 0) {
      return {_layout.mouthX, _layout.mouthY, _layout.mouthW, _layout.mouthH, true};
    }
  if (strcmp(id, "titleStrip") == 0) return {0, 0, _layout.canvasW, 22, true};
    return {};
  }

  AuraRect topBar(int index) const {
    AuraRect r = {_layout.topX, _layout.topY0 + index * _layout.topPitch, _layout.topW, _layout.topH, true};
    return r;
  }

  AuraRect ovalLeft(int index) const {
    return {_layout.ovalLX, _layout.ovalY0 + index * _layout.ovalPitch, _layout.ovalW, _layout.ovalH, true};
  }

  AuraRect ovalRight(int index) const {
    return {_layout.ovalRX, _layout.ovalY0 + index * _layout.ovalPitch, _layout.ovalW, _layout.ovalH, true};
  }

  AuraRect bottomBar(int index) const {
    return {_layout.botX, _layout.botY0 + index * _layout.botPitch, _layout.botW, _layout.botH, true};
  }

  AuraRect hitZoneSettingsP4() const { return ovalRight(3); }

  // Caja real de las 3 columnas del modulador (idéntica al blackout del atlas):
  // así el toque sobre las barras de voz y el clear coinciden con lo dibujado.
  AuraRect modulatorZone() const {
    const int cx[3] = {_layout.colCxLeft, _layout.colCxMid, _layout.colCxRight};
    const int seg[3] = {_layout.colSegSide, _layout.colSegMid, _layout.colSegSide};
    int minX = 9999, minY = 9999, maxX = 0, maxY = 0;
    for (int i = 0; i < 3; i++) {
      const int totalH = seg[i] * _layout.segH + (seg[i] - 1) * _layout.segGap;
      const int topY = _layout.colCenterY - totalH / 2;
      const int x0 = cx[i] - _layout.segW / 2;
      if (x0 < minX) minX = x0;
      if (x0 + _layout.segW > maxX) maxX = x0 + _layout.segW;
      if (topY < minY) minY = topY;
      if (topY + totalH > maxY) maxY = topY + totalH;
    }
    const int pad = 3;
    return {minX - pad, minY - pad, (maxX - minX) + 2 * pad, (maxY - minY) + 2 * pad, true};
  }

  KittButton hitKittButton(int sx, int sy) const {
    if (_kind != ThemeKind::Kitt) return KittButton::None;
    auto hit = [&](AuraRect r) {
      return r.valid && sx >= r.x && sx <= r.x + r.w && sy >= r.y && sy <= r.y + r.h;
    };

    for (int i = 0; i < 4; i++) {
      if (hit(topBar(i))) {
        return (KittButton)((int)KittButton::Power + i);
      }
    }
    for (int i = 0; i < 4; i++) {
      if (hit(ovalLeft(i))) {
        return (KittButton)((int)KittButton::Air + i);
      }
      if (hit(ovalRight(i))) {
        return (KittButton)((int)KittButton::S1 + i);
      }
    }
    for (int i = 0; i < 3; i++) {
      if (hit(bottomBar(i))) {
        return (KittButton)((int)KittButton::AutoCruise + i);
      }
    }
    if (hit(get("modulator"))) return KittButton::Modulator;
    return KittButton::None;
  }

 private:
  AuraLayout _layout;
  ThemeKind _kind = ThemeKind::Kitt;
};

#pragma once

#include "LayoutManager.h"

// Pantalla música KITT — layout propio (no reutiliza botones del tablero normal).
namespace KittMusicLayout {

static constexpr int kW = 240;
static constexpr int kH = 320;
static constexpr int kTitleY = 6;
static constexpr int kTitleH = 22;
static constexpr int kSepTopY = 32;
static constexpr int kModY0 = 38;
static constexpr int kModY1 = 248;
static constexpr int kSepBotY = 252;
static constexpr int kBarY = 258;
static constexpr int kBarH = 54;
static constexpr int kBarMargin = 8;
static constexpr int kBarGap = 4;
static constexpr int kBtnCount = 6;

inline AuraRect titleBar() { return {8, kTitleY, kW - 16, kTitleH, true}; }

inline AuraRect modulatorZone() { return {12, kModY0, kW - 24, kModY1 - kModY0, true}; }

inline AuraRect transportButton(int index) {
  if (index < 0 || index >= kBtnCount) return {};
  const int totalGap = kBarGap * (kBtnCount - 1);
  const int bw = (kW - 2 * kBarMargin - totalGap) / kBtnCount;
  const int x = kBarMargin + index * (bw + kBarGap);
  return {x, kBarY, bw, kBarH, true};
}

// Mapeo a KittButton existente (reutiliza handler de música).
inline KittButton buttonAt(int index) {
  switch (index) {
    case 0: return KittButton::AutoCruise;    // anterior
    case 1: return KittButton::NormalCruise;   // detener
    case 2: return KittButton::Pursuit;        // siguiente
    case 3: return KittButton::MinRpm;         // vol-
    case 4: return KittButton::FuelOn;         // vol+
    case 5: return KittButton::Ignitors;       // radio
    default: return KittButton::None;
  }
}

inline KittButton hit(int sx, int sy) {
  for (int i = 0; i < kBtnCount; i++) {
    AuraRect r = transportButton(i);
    if (r.valid && sx >= r.x && sx < r.x + r.w && sy >= r.y && sy < r.y + r.h) return buttonAt(i);
  }
  return KittButton::None;
}

}  // namespace KittMusicLayout

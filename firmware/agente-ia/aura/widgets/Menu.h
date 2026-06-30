#pragma once

#include <Arduino.h>

struct MenuItem {
  const char *id;
  const char *label;
};

// Menú táctil simple — lista vertical de opciones (overlay Fase 2).
class MenuWidget {
 public:
  void setItems(const MenuItem *items, int count) {
    _items = items;
    _count = count;
  }

  int hitTest(int sx, int sy, int x, int y0, int rowH, int w) const {
    if (!_items || _count <= 0) return -1;
    if (sx < x || sx > x + w) return -1;
    const int idx = (sy - y0) / rowH;
    if (idx < 0 || idx >= _count) return -1;
    return idx;
  }

  const char *itemId(int index) const {
    if (!_items || index < 0 || index >= _count) return nullptr;
    return _items[index].id;
  }

 private:
  const MenuItem *_items = nullptr;
  int _count = 0;
};

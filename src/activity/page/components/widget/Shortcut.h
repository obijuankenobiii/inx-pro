#pragma once

#include <cstdint>

#include "state/HomeTheme.h"

class GfxRenderer;

class Shortcut final {
 public:
  explicit Shortcut(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height, bool compact = false,
              const uint8_t* selectedItems = nullptr) const;
  int hitTest(int x, int y, int areaX, int areaY, int areaW, int areaH, bool compact = false,
              const uint8_t* selectedItems = nullptr) const;
  int itemForVisibleIndex(int visibleIndex, const uint8_t* selectedItems = nullptr) const;
  static const char* itemLabel(int index);

 private:
  GfxRenderer& renderer_;
};

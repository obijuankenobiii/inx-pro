#pragma once

#include <cstddef>;
#include "system/UiLayout.h"

class GfxRenderer;

/** Shared left sidebar frame and simple text-list layout used by top-level pages. */
class Sidebar final {
 public:
  static constexpr int rowHeight = UiLayout::LIST_ITEM_HEIGHT;

  static int width(const GfxRenderer& renderer);
  static int listTop();
  static void renderFrame(const GfxRenderer& renderer, const char* title);
  static void renderTextList(const GfxRenderer& renderer, const char* const* labels, size_t count);
  static int hitTest(const GfxRenderer& renderer, int tapX, int tapY, size_t count);
};

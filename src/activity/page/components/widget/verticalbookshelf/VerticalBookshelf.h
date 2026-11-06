#pragma once

class GfxRenderer;

namespace widget::verticalbookshelf {

/** Two-tier shelf with five books per tier, keeping the newest book forward on top and a random book forward below. */
class VerticalBookshelf final {
 public:
  static void render(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex,
                     bool drawSelection = true);
  static void renderSelection(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex);
  static int hitTest(int x, int y, int width, int height, int tapX, int tapY);
  static int nextSelectionIndex(int currentIndex, int count);
  static int previousSelectionIndex(int currentIndex, int count);
  static void preview(GfxRenderer& renderer, int x, int y, int width, int height);
};

}  // namespace widget::verticalbookshelf

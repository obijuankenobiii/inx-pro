#pragma once

class GfxRenderer;

class RecentGrid final {
 public:
  explicit RecentGrid(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height, int pageIndex = 0) const;
  void preview(int x, int y, int width, int height) const;
  int hitTest(int x, int y, int width, int height, int pageIndex, int tapX, int tapY) const;

 private:
  GfxRenderer& renderer_;
};

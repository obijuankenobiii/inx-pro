#pragma once

class GfxRenderer;

class Shortcut final {
 public:
  explicit Shortcut(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height, bool compact = false) const;
  int hitTest(int x, int y, int areaX, int areaY, int areaW, int areaH, bool compact = false) const;

 private:
  GfxRenderer& renderer_;
};

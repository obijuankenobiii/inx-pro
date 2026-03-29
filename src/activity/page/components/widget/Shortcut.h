#pragma once

class GfxRenderer;

class Shortcut final {
 public:
  explicit Shortcut(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height) const;
  int hitTest(int x, int y, int areaX, int areaY, int areaW, int areaH) const;

 private:
  GfxRenderer& renderer_;
};

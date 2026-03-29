#pragma once

class GfxRenderer;

class Circle {
 public:
  explicit Circle(GfxRenderer& g) : gfx(g) {}
  void render(int centerX, int centerY, int radius, bool state = true) const;

 private:
  GfxRenderer& gfx;
};

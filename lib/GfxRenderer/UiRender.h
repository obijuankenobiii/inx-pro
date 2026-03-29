#pragma once

class GfxRenderer;

class UiRender {
 public:
  explicit UiRender(GfxRenderer& g) : gfx(g) {}

  /** @param topY Row's top edge; defaults to the standard bottom-anchored position (pageHeight - 40). */
  void dottedRect(int x, int y, int width, int height, bool state = true) const;
  void fillSparseInkLatticeInRect(int x, int y, int width, int height, int latticeStep = 2) const;

 private:
  GfxRenderer& gfx;
};

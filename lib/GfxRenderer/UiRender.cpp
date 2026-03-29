#include "UiRender.h"

#include <algorithm>

#include "GfxRenderer.h"

void UiRender::dottedRect(const int x, const int y, const int width, const int height, const bool state) const {
  gfx.rectangle.dotted(x, y, width, height, state);
}

void UiRender::fillSparseInkLatticeInRect(const int x, const int y, const int width, const int height,
                                          const int latticeStep) const {
  if (width <= 0 || height <= 0) {
    return;
  }
  int step = latticeStep;
  if (step < 2) {
    step = 2;
  }
  const bool pow2 = (step & (step - 1)) == 0;
  const int sw = gfx.getScreenWidth();
  const int sh = gfx.getScreenHeight();
  const int x1 = std::max(0, x);
  const int y1 = std::max(0, y);
  const int x2 = std::min(sw, x + width);
  const int y2 = std::min(sh, y + height);
  if (pow2) {
    const int mask = step - 1;
    for (int py = (y1 + step - 1) & ~mask; py < y2; py += step) {
      for (int px = (x1 + step - 1) & ~mask; px < x2; px += step) {
        gfx.drawPixel(px, py, true);
      }
    }
    return;
  }
  for (int py = y1; py < y2; py += step) {
    for (int px = x1; px < x2; px += step) {
      gfx.drawPixel(px, py, true);
    }
  }
}

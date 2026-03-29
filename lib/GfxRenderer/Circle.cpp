#include "Circle.h"

#include "GfxRenderer.h"

void Circle::render(const int centerX, const int centerY, const int radius, const bool state) const {
  if (radius < 0) {
    return;
  }

  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (dx * dx + dy * dy <= radius * radius) {
        gfx.drawPixel(centerX + dx, centerY + dy, state);
      }
    }
  }
}

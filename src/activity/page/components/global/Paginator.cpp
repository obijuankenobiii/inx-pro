#include "Paginator.h"

#include <GfxRenderer.h>

#include <algorithm>

namespace {
constexpr int kRadius = 6;
constexpr int kGap = 12;
}

void Paginator::render(GfxRenderer& renderer, const int activePage, const int pageCount, const int centerX,
                       const int top, const int height) {
  if (pageCount <= 1 || height <= 0) return;

  const int page = std::max(0, std::min(activePage, pageCount - 1));
  const int indicatorHeight = pageCount * kRadius * 2 + (pageCount - 1) * kGap;
  const int indicatorTop = top + std::max(0, (height - indicatorHeight) / 2);
  for (int index = 0; index < pageCount; ++index) {
    const int centerY = indicatorTop + kRadius + index * (kRadius * 2 + kGap);
    renderer.circle.render(centerX, centerY, kRadius, true);
    if (index != page) {
      renderer.circle.render(centerX, centerY, kRadius - 2, false);
    }
  }
}

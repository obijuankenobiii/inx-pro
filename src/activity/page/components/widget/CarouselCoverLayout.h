#pragma once

#include <algorithm>

inline constexpr int kCarouselSideReduction = 10;
inline constexpr int kCarouselCenterReduction = 10;

struct CarouselCoverSize {
  int width, height;
};

// All centered carousel cards use a 2:3 portrait frame. Crop-to-fill preserves
// the image's aspect ratio while filling this frame without stretching it.
inline CarouselCoverSize carouselCoverSize(int maxWidth, int maxHeight) {
  maxWidth = std::max(1, maxWidth);
  maxHeight = std::max(1, maxHeight);
  const int height = std::max(1, std::min(maxHeight, maxWidth * 3 / 2));
  return {std::max(1, std::min(maxWidth, (height * 2 + 1) / 3)), height};
}

struct CenteredCarouselBounds {
  int centerX, centerY, centerWidth, centerHeight;
  int sideX, sideY, sideWidth, sideHeight;
  int gap;
};

inline void reduceCarouselCenter(int& x, int& y, int& width, int& height) {
  const auto smaller = carouselCoverSize(width, height - kCarouselCenterReduction);
  x += (width - smaller.width) / 2;
  y += (height - smaller.height) / 2;
  width = smaller.width;
  height = smaller.height;
}

inline CenteredCarouselBounds reduceCarouselCenter(CenteredCarouselBounds layout) {
  const int previousRight = layout.centerX + layout.centerWidth;
  reduceCarouselCenter(layout.centerX, layout.centerY, layout.centerWidth, layout.centerHeight);
  // Right-hand cards are positioned relative to the center's right edge.
  // Preserve their position when reducing only the center card.
  layout.gap += previousRight - (layout.centerX + layout.centerWidth);
  return layout;
}

inline CenteredCarouselBounds centeredCarouselBounds(int x, int y, int width, int height, bool fullHeight,
                                                     int maxWidth, int maxHeight, int sideReduction) {
  sideReduction += kCarouselSideReduction;
  const int gap = width >= 360 ? 12 : 6;
  int centerWidthLimit = std::min(maxWidth, std::max(40, width * 3 / 5));
  if ((width - centerWidthLimit - gap * 2) / 2 < 24)
    centerWidthLimit = std::max(40, width - gap * 2 - 48);
  const auto center = carouselCoverSize(centerWidthLimit,
                                       std::min(maxHeight, std::max(50, height - (fullHeight ? 10 : 28))));
  const auto originalSide = carouselCoverSize(center.width, center.height * 9 / 10);
  const auto side = sideReduction > 0 ? carouselCoverSize(originalSide.width, originalSide.height - sideReduction)
                                     : originalSide;
  const int centerX = x + (width - center.width) / 2;
  const int centerY = y + (height - center.height) / 2;
  const int sideInset = (originalSide.width - side.width) / 2;
  return reduceCarouselCenter({centerX, centerY, center.width, center.height,
          centerX - originalSide.width - gap + sideInset, y + (height - side.height) / 2,
          side.width, side.height, gap + sideInset});
}

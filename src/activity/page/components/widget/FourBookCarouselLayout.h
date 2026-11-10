#pragma once

#include <algorithm>
#include <array>
#include "CarouselCoverLayout.h"

// Left-to-right: the previous cover, the active (largest) cover, the next
// cover, and the following cover. The outer covers extend beyond the viewport.
struct FourBookCarouselCard {
  int x, y, width, height;
  int offset;
  float cropAnchor;
  int coverWidth;

  int coverX() const { return offset == -1 ? x + width - coverWidth : x; }

  bool contains(int px, int py) const {
    return px >= x && px < x + width && py >= y && py < y + height;
  }
};

inline std::array<FourBookCarouselCard, 4> fourBookCarouselLayout(int x, int y, int width, int height,
                                                               bool fullHeight, int sideReduction = 0) {
  // Keep thumbnail dimensions independent of spacing. Packing the same covers
  // more tightly exposes more of both outer books without resizing any cover.
  const int sizeGap = width >= 360 ? 6 : 4;
  const int gap = width >= 360 ? 4 : 2;
  const int available = std::max(4, width - sizeGap * 3);
  const auto main = carouselCoverSize(available * 5 / 13, std::min(300, height - (fullHeight ? 6 : 8)));
  const auto side = carouselCoverSize(main.width, main.height * 9 / 10);
  const int mainWidth = main.width;
  const int mainHeight = main.height;
  const int sideWidth = side.width;
  const int sideHeight = side.height;
  const int mainY = y + (height - mainHeight) / 2;
  const int sideY = y + (height - sideHeight) / 2;
  const int stripWidth = mainWidth + sideWidth * 3 + gap * 3;
  const int firstX = x + (width - stripWidth) / 2;
  const int mainX = firstX + sideWidth + gap;
  const int nextX = mainX + mainWidth + gap;
  const int lastX = nextX + sideWidth + gap;
  const int firstVisibleX = std::max(x, firstX);
  const int firstVisibleWidth = std::min(x + width, firstX + sideWidth) - firstVisibleX;
  const int lastVisibleWidth = std::min(sideWidth, x + width - lastX);
  std::array<FourBookCarouselCard, 4> cards{{{firstVisibleX, sideY, firstVisibleWidth, sideHeight, -1, 0.5f, sideWidth},
           {mainX, mainY, mainWidth, mainHeight, 0, 0.5f, mainWidth},
           {nextX, sideY, sideWidth, sideHeight, 1, 0.5f, sideWidth},
           {lastX, sideY, lastVisibleWidth, sideHeight, 2, 0.5f, sideWidth}}};
  sideReduction += kCarouselSideReduction;
  if (sideReduction > 0) {
    for (auto& card : cards) {
      if (card.offset == 0) continue;
      const auto smaller = carouselCoverSize(card.coverWidth, card.height - sideReduction);
      const int smallerHeight = smaller.height;
      const int smallerWidth = smaller.width;
      const int coverX = card.coverX() + (card.coverWidth - smallerWidth) / 2;
      card.y += (card.height - smallerHeight) / 2;
      card.height = smallerHeight;
      card.coverWidth = smallerWidth;
      card.x = std::max(x, coverX);
      card.width = std::max(0, std::min(x + width, coverX + smallerWidth) - card.x);
    }
  }
  auto& center = cards[1];
  reduceCarouselCenter(center.x, center.y, center.coverWidth, center.height);
  center.width = center.coverWidth;
  return cards;
}

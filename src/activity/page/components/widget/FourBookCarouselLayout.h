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
  const int verticalPadding = fullHeight ? 0 : 10;
  if (!fullHeight) {
    y += verticalPadding;
    height = std::max(1, height - verticalPadding * 2);
  }

  // Keep the full-height variant tight, while giving the regular variant
  // 10pt top/bottom padding and 10pt gaps between thumbnails.
  const int gap = fullHeight ? (width >= 360 ? 4 : 2) : 10;
  const int available = std::max(4, width - gap * 3);
  const auto main = carouselCoverSize(available * 5 / 13, std::min(300, height - (fullHeight ? 6 : 8)));
  // The 1x3 carousel keeps every thumbnail on the same visual baseline.  Do
  // not make the second (center) thumbnail taller than the others.
  const auto side = main;
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
  // Keep the four visible cards uniform.  The parameter is retained for the
  // callers' API compatibility, but must not reintroduce unequal heights.
  (void)sideReduction;
  return cards;
}

// Labelled 1x3 carousels show three full cards with a partial preview at each
// edge. The current book remains the first full card, matching the carousel's
// existing index/gesture behavior.
inline std::array<FourBookCarouselCard, 5> fiveBookCarouselLayout(int x, int y, int width, int height,
                                                                    bool fullHeight) {
  const int verticalPadding = fullHeight ? 0 : 10;
  if (!fullHeight) {
    y += verticalPadding;
    height = std::max(1, height - verticalPadding * 2);
  }

  const int gap = fullHeight ? (width >= 360 ? 4 : 2) : 10;
  const int available = std::max(4, width - gap * 4);
  const int maxHeight = std::max(1, height - (fullHeight ? 6 : 8));
  // Size the three full middle cards to nearly fill the row. This leaves only
  // a small edge sliver for the first and last preview cards.
  const auto card = carouselCoverSize(available / 3, std::min(300, maxHeight));
  const int cardWidth = card.width;
  const int cardHeight = card.height;
  const int stripWidth = cardWidth * 5 + gap * 4;
  const int firstX = x + (width - stripWidth) / 2;
  const int cardY = y + (height - cardHeight) / 2;
  const int firstVisibleX = std::max(x, firstX);
  const int firstVisibleWidth = std::min(x + width, firstX + cardWidth) - firstVisibleX;
  const int lastX = firstX + 4 * (cardWidth + gap);
  const int lastVisibleWidth = std::min(cardWidth, x + width - lastX);

  return {{{firstVisibleX, cardY, firstVisibleWidth, cardHeight, -1, 0.5f, cardWidth},
           {firstX + cardWidth + gap, cardY, cardWidth, cardHeight, 0, 0.5f, cardWidth},
           {firstX + 2 * (cardWidth + gap), cardY, cardWidth, cardHeight, 1, 0.5f, cardWidth},
           {firstX + 3 * (cardWidth + gap), cardY, cardWidth, cardHeight, 2, 0.5f, cardWidth},
           {lastX, cardY, lastVisibleWidth, cardHeight, 3, 0.5f, cardWidth}}};
}

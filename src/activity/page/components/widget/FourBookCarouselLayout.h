#pragma once

#include <algorithm>
#include <array>
#include "CarouselCoverLayout.h"



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



  constexpr int gap = 10;
  const int sizingGap = fullHeight ? (width >= 360 ? 4 : 2) : gap;
  const int available = std::max(4, width - sizingGap * 3);
  const auto main = carouselCoverSize(available * 5 / 13, std::min(300, height - (fullHeight ? 6 : 8)));


  const auto side = main;
  const int mainWidth = main.width;
  const int mainHeight = main.height;
  const int sideWidth = side.width;
  const int sideHeight = side.height;
  const int mainY = y + (height - mainHeight) / 2;
  const int sideY = y + (height - sideHeight) / 2;
  const int sizingStripWidth = mainWidth + sideWidth * 3 + sizingGap * 3;
  const int sizingFirstX = x + (width - sizingStripWidth) / 2;
  const int mainX = sizingFirstX + sideWidth + sizingGap;
  const int firstX = mainX - sideWidth - gap;
  const int nextX = mainX + mainWidth + gap;
  const int lastX = nextX + sideWidth + gap;
  const int firstVisibleX = std::max(x, firstX);
  const int firstVisibleWidth = std::min(x + width, firstX + sideWidth) - firstVisibleX;
  const int lastVisibleWidth = std::min(sideWidth, x + width - lastX);
  std::array<FourBookCarouselCard, 4> cards{{{firstVisibleX, sideY, firstVisibleWidth, sideHeight, -1, 0.5f, sideWidth},
           {mainX, mainY, mainWidth, mainHeight, 0, 0.5f, mainWidth},
           {nextX, sideY, sideWidth, sideHeight, 1, 0.5f, sideWidth},
           {lastX, sideY, lastVisibleWidth, sideHeight, 2, 0.5f, sideWidth}}};


  (void)sideReduction;
  return cards;
}




inline std::array<FourBookCarouselCard, 5> fiveBookCarouselLayout(int x, int y, int width, int height,
                                                                    bool fullHeight) {
  const int verticalPadding = fullHeight ? 0 : 10;
  if (!fullHeight) {
    y += verticalPadding;
    height = std::max(1, height - verticalPadding * 2);
  }

  constexpr int gap = 10;
  const int sizingGap = fullHeight ? (width >= 360 ? 4 : 2) : gap;
  const int available = std::max(4, width - sizingGap * 4);
  const int maxHeight = std::max(1, height - (fullHeight ? 6 : 8));


  const auto card = carouselCoverSize(available / 3, std::min(300, maxHeight));
  const int cardWidth = card.width;
  const int cardHeight = card.height;
  const int sizingStripWidth = cardWidth * 5 + sizingGap * 4;
  const int sizingFirstX = x + (width - sizingStripWidth) / 2;
  const int cardY = y + (height - cardHeight) / 2;
  const int firstFullX = sizingFirstX + cardWidth + sizingGap;
  const int firstX = firstFullX - cardWidth - gap;
  const int firstVisibleX = std::max(x, firstX);
  const int firstVisibleWidth = std::min(x + width, firstX + cardWidth) - firstVisibleX;
  const int lastX = firstFullX + 3 * (cardWidth + gap);
  const int lastVisibleWidth = std::min(cardWidth, x + width - lastX);

  return {{{firstVisibleX, cardY, firstVisibleWidth, cardHeight, -1, 0.5f, cardWidth},
           {firstFullX, cardY, cardWidth, cardHeight, 0, 0.5f, cardWidth},
           {firstFullX + cardWidth + gap, cardY, cardWidth, cardHeight, 1, 0.5f, cardWidth},
           {firstFullX + 2 * (cardWidth + gap), cardY, cardWidth, cardHeight, 2, 0.5f, cardWidth},
           {lastX, cardY, lastVisibleWidth, cardHeight, 3, 0.5f, cardWidth}}};
}

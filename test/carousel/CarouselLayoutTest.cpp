#include "activity/page/components/widget/FourBookCarouselLayout.h"

#include <cassert>
#include <cmath>
#include <cstdio>

static void portrait(int width, int height) {
  assert(width > 0 && height > 0);
  assert(std::abs(width * 3 - height * 2) <= 1);  // integer rounding only
}

int main() {
  const CenteredCarouselBounds before{100, 80, 200, 300, -90, 95, 180, 270, 12};
  const auto after = reduceCarouselCenter(before);
  assert(after.centerHeight == before.centerHeight - 10);
  portrait(after.centerWidth, after.centerHeight);
  assert(after.sideX == before.sideX && after.sideY == before.sideY);
  assert(after.sideWidth == before.sideWidth && after.sideHeight == before.sideHeight);
  assert(after.centerX + after.centerWidth + after.gap == before.centerX + before.centerWidth + before.gap);

  unsigned cases = 0;
  for (int width : {200, 240, 320, 360, 480, 540, 600, 800, 960}) {
    for (int height : {80, 140, 190, 220, 260, 340, 620}) {
      for (bool fullHeight : {false, true}) {
        for (int maxWidth : {200, 204}) {
          auto normal = centeredCarouselBounds(13, 27, width, height, fullHeight, maxWidth, 312, 0);
          auto hidden = centeredCarouselBounds(13, 27, width, height, fullHeight, maxWidth, 312, 5);
          assert(hidden.centerX == normal.centerX && hidden.centerY == normal.centerY);
          assert(hidden.centerWidth == normal.centerWidth && hidden.centerHeight == normal.centerHeight);
          assert(hidden.sideHeight == normal.sideHeight - 5);
          for (const auto& layout : {normal, hidden}) {
            portrait(layout.centerWidth, layout.centerHeight);
            portrait(layout.sideWidth, layout.sideHeight);
            assert(layout.centerHeight > layout.sideHeight && layout.centerWidth > layout.sideWidth);
            assert(layout.sideX + layout.sideWidth < layout.centerX);
            assert(layout.centerY >= 27 && layout.centerY + layout.centerHeight <= 27 + height);
          }
          ++cases;
        }
        auto normal = fourBookCarouselLayout(13, 27, width, height, fullHeight);
        auto hidden = fourBookCarouselLayout(13, 27, width, height, fullHeight, 5);
        assert(normal[1].offset == 0 && normal[1].width == hidden[1].width);
        assert(normal[1].height == hidden[1].height && normal[1].x == hidden[1].x && normal[1].y == hidden[1].y);
        for (int i = 0; i < 4; ++i) {
          for (const auto& cards : {normal, hidden}) {
            const auto& card = cards[i];
            portrait(card.coverWidth, card.height);
            assert(card.width > 0 && card.x >= 13 && card.x + card.width <= 13 + width);
            assert(card.contains(card.x + card.width / 2, card.y + card.height / 2));
            assert(!card.contains(card.x + card.width, card.y));
            if (i != 1) assert(card.height < cards[1].height && card.coverWidth < cards[1].coverWidth);
            if (i > 0) assert(cards[i - 1].x + cards[i - 1].width <= card.x);
          }
          if (i != 1) assert(hidden[i].height == normal[i].height - 5);
        }
        ++cases;
      }
    }
  }
  std::printf("%u centered carousel geometry cases passed\n", cases);
}

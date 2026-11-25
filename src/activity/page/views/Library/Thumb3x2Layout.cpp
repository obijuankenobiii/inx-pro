#include "Thumb3x2Layout.h"

#include <algorithm>

namespace views {
namespace library {

namespace {
constexpr int kThreeByTwoCellHeight = 312;
constexpr int kThreeByTwoRowGap = 10;

int threeByTwoCellHeight(const int cellWidth, const int fullHeight) {
  (void)cellWidth;
  (void)fullHeight;
  return kThreeByTwoCellHeight;
}
}

ThumbLayout thumb3x2Layout() { return {3, 2, threeByTwoCellHeight, kThreeByTwoRowGap, true, true, 2, 10, 5, 10}; }

}
}

#include "Thumb3x2Layout.h"

#include <algorithm>

namespace views {
namespace library {

namespace {
int threeByTwoCellHeight(const int cellWidth, const int fullHeight) {
  (void)cellWidth;
  (void)fullHeight;
  constexpr int fixedHeight = 305;
  return fixedHeight;
}
}

ThumbLayout thumb3x2Layout() { return {3, 2, threeByTwoCellHeight, 10, true, true, 2, 10, 5, 20}; }

}
}

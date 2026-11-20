#include "Thumb3x2Layout.h"

#include <algorithm>

namespace views {
namespace library {

namespace {
int threeByTwoCellHeight(const int cellWidth, const int fullHeight) {
  (void)cellWidth;
  (void)fullHeight;
  constexpr int fixedHeight = 300;
  return fixedHeight;
}
}  // namespace

ThumbLayout thumb3x2Layout() { return {3, 2, threeByTwoCellHeight, 20, true, true, 2, 5, 12, 20}; }

}  // namespace library
}  // namespace views

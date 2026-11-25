#include "Thumb3x2Layout.h"

#include <algorithm>

namespace views {
namespace library {

namespace {
#if FREEINK_DEVICE_MURPHY_M4
constexpr int kThreeByTwoCellHeight = 305;
constexpr int kThreeByTwoRowGap = 10;
#else
constexpr int kThreeByTwoCellHeight = 300;
constexpr int kThreeByTwoRowGap = 30;
#endif

int threeByTwoCellHeight(const int cellWidth, const int fullHeight) {
  (void)cellWidth;
  (void)fullHeight;
  return kThreeByTwoCellHeight;
}
}

ThumbLayout thumb3x2Layout() { return {3, 2, threeByTwoCellHeight, kThreeByTwoRowGap, true, true, 2, 10, 5, 20}; }

}
}

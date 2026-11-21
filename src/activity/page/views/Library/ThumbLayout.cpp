#include "ThumbLayout.h"

namespace views {
namespace library {

namespace {
int standardCellHeight(const int, const int fullHeight) { return fullHeight; }
}

ThumbLayout standardThumbLayout() { return {2, 2, standardCellHeight, 10, false, false, 1, 0, 20, 0}; }

}
}

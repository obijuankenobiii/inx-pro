#pragma once

namespace views {
namespace library {

struct ThumbLayout {
  int columns = 2;
  int rows = 2;
  int (*cellHeight)(int cellWidth, int fullHeight) = nullptr;
  int rowGap = 10;
  bool inlineFolderCount = false;
  bool alignTop = false;
  int titleLines = 1;
  int cellWidthExtra = 0;
  int columnGap = 20;
  int topGap = 0;
};

ThumbLayout standardThumbLayout();

}
}

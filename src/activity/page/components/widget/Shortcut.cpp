#include "Shortcut.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdint>

#include "images/BookmarkIcon.h"
#include "images/FavoriteIcon.h"
#include "images/HighlightIcon.h"
#include "images/StatisticsIcon.h"
#include "system/Fonts.h"

namespace {

struct ShortcutItem {
  const char* label;
  const uint8_t* icon;
};

constexpr ShortcutItem kItems[] = {{"Bookmarks", BookmarkIcon}, {"Highlights", HighlightIcon},
                                   {"Favorites", FavoriteIcon}, {"Statistics", StatisticsIcon}};
constexpr int kItemCount = static_cast<int>(sizeof(kItems) / sizeof(kItems[0]));
constexpr int kColumns = 2;
constexpr int kRows = 2;

}  // namespace

void Shortcut::render(const int x, const int y, const int width, const int height) const {
  constexpr int innerPadding = 16;
  constexpr int topPadding = 40;
  constexpr int gap = 20;
  constexpr int iconSize = 40;
  const int cellWidth = std::max(1, (width - innerPadding * 2 - gap * (kColumns - 1)) / kColumns);
  const int cellHeight = std::max(1, (height - topPadding - innerPadding - gap * (kRows - 1)) / kRows);
  const int font = systemFontId();
  const int lineHeight = renderer_.text.getLineHeight(font);
  for (int i = 0; i < kItemCount; ++i) {
    const int cellX = x + innerPadding + (i % kColumns) * (cellWidth + gap);
    const int cellY = y + topPadding + (i / kColumns) * (cellHeight + gap);
    const int iconX = cellX + (cellWidth - iconSize) / 2;
    const int iconY = cellY + std::max(8, (cellHeight - iconSize - lineHeight - 12) / 2);
    renderer_.bitmap.icon(kItems[i].icon, iconX, iconY, iconSize, iconSize);
    const int textWidth = renderer_.text.getWidth(font, kItems[i].label);
    renderer_.text.render(font, cellX + (cellWidth - textWidth) / 2, iconY + iconSize + 12, kItems[i].label, true);
  }
}

int Shortcut::hitTest(const int tapX, const int tapY, const int areaX, const int areaY, const int areaW,
                      const int areaH) const {
  constexpr int innerPadding = 16;
  constexpr int topPadding = 40;
  constexpr int gap = 20;
  const int paddedCellWidth = std::max(1, (areaW - innerPadding * 2 - gap * (kColumns - 1)) / kColumns);
  const int gridCellHeight = std::max(1, (areaH - topPadding - innerPadding - gap * (kRows - 1)) / kRows);
  if (tapX < areaX || tapX >= areaX + areaW || tapY < areaY || tapY >= areaY + areaH) return -1;
  if (tapX < areaX + innerPadding || tapX >= areaX + areaW - innerPadding || tapY < areaY + topPadding ||
      tapY >= areaY + areaH - innerPadding)
    return -1;
  const int column = (tapX - areaX - innerPadding) / (paddedCellWidth + gap);
  const int row = (tapY - areaY - topPadding) / (gridCellHeight + gap);
  if (column < 0 || column >= kColumns || row < 0 || row >= kRows) return -1;
  const int cellX = areaX + innerPadding + column * (paddedCellWidth + gap);
  const int cellY = areaY + topPadding + row * (gridCellHeight + gap);
  if (tapX < cellX || tapX >= cellX + paddedCellWidth || tapY < cellY || tapY >= cellY + gridCellHeight)
    return -1;
  const int index = row * kColumns + column;
  return index < kItemCount ? index : -1;
}

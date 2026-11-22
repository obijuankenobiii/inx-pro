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

}

void Shortcut::render(const int x, const int y, const int width, const int height, const bool compact) const {
  constexpr int innerPadding = 16;
  constexpr int topPadding = 40;
  constexpr int gap = 20;
  constexpr int iconSize = 40;
  if (compact) {
    constexpr int compactPadding = 10;
    constexpr int compactTopPadding = 8;
    constexpr int compactBottomPadding = 8;
    constexpr int compactGap = 8;
    constexpr int compactIconLabelGap = 8;
    constexpr int sourceIconSize = 40;
    const int cellWidth = std::max(1, (width - compactPadding * 2 - gap * (kColumns - 1)) / kColumns);
    const int cellHeight = std::max(1, (height - compactTopPadding - compactBottomPadding - compactGap) / kRows);
    const int font = systemFontId();
    const int lineHeight = renderer_.text.getLineHeight(font);
    const int maxIconSize = std::max(1, std::min({28, cellHeight - 2, cellWidth - compactIconLabelGap - 1}));
    const uint8_t iconScale = static_cast<uint8_t>(std::max(1, std::min(100, maxIconSize * 100 / sourceIconSize)));
    const int compactIconSize = std::max(1, (sourceIconSize * iconScale + 50) / 100);
    const int labelMaxWidth = std::max(1, cellWidth - compactIconSize - compactIconLabelGap);
    int maxFullLabelWidth = 1;
    for (const ShortcutItem& item : kItems) {
      maxFullLabelWidth = std::max(maxFullLabelWidth, renderer_.text.getWidth(font, item.label));
    }
    const int maxLabelHeight = std::max(1, cellHeight - 2);
    const uint8_t labelScale = static_cast<uint8_t>(std::max(
        1, std::min({100, labelMaxWidth * 100 / maxFullLabelWidth,
                     maxLabelHeight * 100 / std::max(1, lineHeight)})));
    int groupLabelWidth = 1;
    for (const ShortcutItem& item : kItems) {
      groupLabelWidth = std::max(groupLabelWidth, renderer_.text.getScaledWidth(font, item.label, labelScale));
    }
    const int contentWidth = compactIconSize + compactIconLabelGap + groupLabelWidth;
    const int scaledLineHeight = std::max(1, (lineHeight * labelScale + 50) / 100);
    for (int i = 0; i < kItemCount; ++i) {
      const int cellX = x + compactPadding + (i % kColumns) * (cellWidth + gap);
      const int cellY = y + compactTopPadding + (i / kColumns) * (cellHeight + compactGap);
      const int contentX = cellX + std::max(0, (cellWidth - contentWidth) / 2);
      const int centerY = cellY + cellHeight / 2;
      renderer_.bitmap.iconScaled(kItems[i].icon, contentX, centerY - compactIconSize / 2,
                                  sourceIconSize, sourceIconSize, compactIconSize, compactIconSize);
      renderer_.text.renderScaled(font, contentX + compactIconSize + compactIconLabelGap,
                                  centerY - scaledLineHeight / 2, kItems[i].label, labelScale, true);
    }
    return;
  }
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
                      const int areaH, const bool compact) const {
  constexpr int gap = 20;
  if (compact) {
    constexpr int compactPadding = 10;
    constexpr int compactTopPadding = 8;
    constexpr int compactBottomPadding = 8;
    constexpr int compactGap = 8;
    const int cellWidth = std::max(1, (areaW - compactPadding * 2 - gap * (kColumns - 1)) / kColumns);
    const int cellHeight = std::max(1, (areaH - compactTopPadding - compactBottomPadding - compactGap) / kRows);
    if (tapX < areaX + compactPadding || tapX >= areaX + areaW - compactPadding ||
        tapY < areaY + compactTopPadding || tapY >= areaY + areaH - compactBottomPadding)
      return -1;
    const int column = (tapX - areaX - compactPadding) / (cellWidth + gap);
    const int row = (tapY - areaY - compactTopPadding) / (cellHeight + compactGap);
    if (column < 0 || column >= kColumns || row < 0 || row >= kRows) return -1;
    const int cellX = areaX + compactPadding + column * (cellWidth + gap);
    const int cellY = areaY + compactTopPadding + row * (cellHeight + compactGap);
    if (tapX >= cellX && tapX < cellX + cellWidth && tapY >= cellY && tapY < cellY + cellHeight) {
      const int index = row * kColumns + column;
      return index < kItemCount ? index : -1;
    }
    return -1;
  }
  constexpr int innerPadding = 16;
  constexpr int topPadding = 40;
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

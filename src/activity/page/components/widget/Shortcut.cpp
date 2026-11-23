#include "Shortcut.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdint>

#include "images/Apps.h"
#include "images/BookmarkIcon.h"
#include "images/FavoriteIcon.h"
#include "images/HighlightIcon.h"
#include "images/Library.h"
#include "images/Recent.h"
#include "images/Search.h"
#include "images/Setting.h"
#include "images/StatisticsIcon.h"
#include "images/Sync.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"

namespace {

struct ShortcutItem {
  const char* label;
  const uint8_t* icon;
};

constexpr ShortcutItem kItems[] = {
    {"Bookmarks", BookmarkIcon}, {"Highlights", HighlightIcon}, {"Favorites", FavoriteIcon},
    {"Statistics", StatisticsIcon}, {"Home", Recent}, {"Library", Library}, {"Settings", Setting},
    {"Device", Sync}, {"Apps", Apps}, {"Search", Search},
};
constexpr int kColumns = 2;
constexpr int kRows = 2;

const uint8_t kDefaultItems[HomeTheme::kShortcutSlotCount] = {
    static_cast<uint8_t>(HomeTheme::ShortcutItem::Bookmarks),
    static_cast<uint8_t>(HomeTheme::ShortcutItem::Highlights),
    static_cast<uint8_t>(HomeTheme::ShortcutItem::Favorites),
    static_cast<uint8_t>(HomeTheme::ShortcutItem::Statistics),
};

int selectedItems(const uint8_t* configured, int (&visible)[HomeTheme::kShortcutSlotCount]) {
  const uint8_t* items = configured ? configured : kDefaultItems;
  int count = 0;
  for (int slot = 0; slot < HomeTheme::kShortcutSlotCount; ++slot) {
    const int item = items[slot];
    if (item >= HomeTheme::kShortcutItemCount) continue;
    if (SETTINGS.disableStatistics != 0 && item == static_cast<int>(HomeTheme::ShortcutItem::Statistics)) continue;
    bool duplicate = false;
    for (int index = 0; index < count; ++index) {
      if (visible[index] == item) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) visible[count++] = item;
  }
  return count;
}

const ShortcutItem& itemAt(const int index) {
  return kItems[std::max(0, std::min(index, HomeTheme::kShortcutItemCount - 1))];
}

}

const char* Shortcut::itemLabel(const int index) { return itemAt(index).label; }

int Shortcut::itemForVisibleIndex(const int visibleIndex, const uint8_t* selectedItemsConfig) const {
  int visible[HomeTheme::kShortcutSlotCount] = {};
  const int itemCount = selectedItems(selectedItemsConfig, visible);
  if (visibleIndex < 0 || visibleIndex >= itemCount) return -1;
  return visible[visibleIndex];
}

void Shortcut::render(const int x, const int y, const int width, const int height, const bool compact,
                     const uint8_t* selectedItemsConfig) const {
  constexpr int innerPadding = 16;
  constexpr int topPadding = 40;
  constexpr int gap = 20;
  constexpr int iconSize = 40;
  int visible[HomeTheme::kShortcutSlotCount] = {};
  const int itemCount = selectedItems(selectedItemsConfig, visible);
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
    int maxFullLabelWidth = 1;
    for (int index = 0; index < itemCount; ++index) {
      maxFullLabelWidth = std::max(maxFullLabelWidth, renderer_.text.getWidth(font, itemAt(visible[index]).label));
    }
    const int labelMaxWidth = std::max(1, cellWidth - compactIconSize - compactIconLabelGap);
    const int maxLabelHeight = std::max(1, cellHeight - 2);
    const uint8_t labelScale = static_cast<uint8_t>(std::max(
        1, std::min({100, labelMaxWidth * 100 / maxFullLabelWidth,
                     maxLabelHeight * 100 / std::max(1, lineHeight)})));
    int groupLabelWidth = 1;
    for (int index = 0; index < itemCount; ++index) {
      groupLabelWidth = std::max(groupLabelWidth,
                                 renderer_.text.getScaledWidth(font, itemAt(visible[index]).label, labelScale));
    }
    const int contentWidth = compactIconSize + compactIconLabelGap + groupLabelWidth;
    const int scaledLineHeight = std::max(1, (lineHeight * labelScale + 50) / 100);
    for (int index = 0; index < itemCount; ++index) {
      const ShortcutItem& item = itemAt(visible[index]);
      const int cellX = x + compactPadding + (index % kColumns) * (cellWidth + gap);
      const int cellY = y + compactTopPadding + (index / kColumns) * (cellHeight + compactGap);
      const int contentX = cellX + std::max(0, (cellWidth - contentWidth) / 2);
      const int centerY = cellY + cellHeight / 2;
      renderer_.bitmap.iconScaled(item.icon, contentX, centerY - compactIconSize / 2,
                                  sourceIconSize, sourceIconSize, compactIconSize, compactIconSize);
      renderer_.text.renderScaled(font, contentX + compactIconSize + compactIconLabelGap,
                                  centerY - scaledLineHeight / 2, item.label, labelScale, true);
    }
    return;
  }
  const int cellWidth = std::max(1, (width - innerPadding * 2 - gap * (kColumns - 1)) / kColumns);
  const int cellHeight = std::max(1, (height - topPadding - innerPadding - gap * (kRows - 1)) / kRows);
  const int font = systemFontId();
  const int lineHeight = renderer_.text.getLineHeight(font);
  for (int index = 0; index < itemCount; ++index) {
    const ShortcutItem& item = itemAt(visible[index]);
    const int cellX = x + innerPadding + (index % kColumns) * (cellWidth + gap);
    const int cellY = y + topPadding + (index / kColumns) * (cellHeight + gap);
    const int iconX = cellX + (cellWidth - iconSize) / 2;
    const int iconY = cellY + std::max(8, (cellHeight - iconSize - lineHeight - 12) / 2);
    renderer_.bitmap.icon(item.icon, iconX, iconY, iconSize, iconSize);
    const int textWidth = renderer_.text.getWidth(font, item.label);
    renderer_.text.render(font, cellX + (cellWidth - textWidth) / 2, iconY + iconSize + 12, item.label, true);
  }
}

int Shortcut::hitTest(const int tapX, const int tapY, const int areaX, const int areaY, const int areaW,
                      const int areaH, const bool compact, const uint8_t* selectedItemsConfig) const {
  constexpr int gap = 20;
  int visible[HomeTheme::kShortcutSlotCount] = {};
  const int itemCount = selectedItems(selectedItemsConfig, visible);
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
    const int index = row * kColumns + column;
    const int cellX = areaX + compactPadding + column * (cellWidth + gap);
    const int cellY = areaY + compactTopPadding + row * (cellHeight + compactGap);
    if (index < itemCount && tapX >= cellX && tapX < cellX + cellWidth && tapY >= cellY && tapY < cellY + cellHeight)
      return index;
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
  const int index = row * kColumns + column;
  const int cellX = areaX + innerPadding + column * (paddedCellWidth + gap);
  const int cellY = areaY + topPadding + row * (gridCellHeight + gap);
  if (index < itemCount && tapX >= cellX && tapX < cellX + paddedCellWidth && tapY >= cellY &&
      tapY < cellY + gridCellHeight)
    return index;
  return -1;
}

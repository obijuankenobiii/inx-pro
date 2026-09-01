#include "ShortcutList.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdint>

#include "images/BookmarkIcon.h"
#include "images/BookAtlas.h"
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
                                   {"Favorites", FavoriteIcon}, {"Statistics", StatisticsIcon},
                                   {"Dictionary", BookAtlas}};
constexpr int kItemCount = static_cast<int>(sizeof(kItems) / sizeof(kItems[0]));

constexpr int kInnerPadding = 16;
constexpr int kTopPadding = 24;
constexpr int kRowGap = 8;
constexpr int kIconSize = 40;

int rowHeight(const int height, const int lineHeight) {
  return std::max(lineHeight + 12,
                  (height - kTopPadding - kInnerPadding - kRowGap * (kItemCount - 1)) / kItemCount);
}

}

void ShortcutList::render(const int x, const int y, const int width, const int height,
                          const int rowHeightOverride) const {
  const int font = systemFontId();
  const int lineHeight = renderer_.text.getLineHeight(font);
  const int row = rowHeightOverride > 0 ? rowHeightOverride : rowHeight(height, lineHeight);
  for (int i = 0; i < kItemCount; ++i) {
    const int rowX = x + kInnerPadding;
    const int rowY = y + kTopPadding + i * (row + kRowGap);
    const int iconX = rowX + 8;
    const int iconY = rowY + (row - kIconSize) / 2;
    renderer_.bitmap.icon(kItems[i].icon, iconX, iconY, kIconSize, kIconSize);
    renderer_.text.render(font, iconX + kIconSize + 16,
                          rowY + (row - lineHeight) / 2, kItems[i].label, true);
  }
}

int ShortcutList::hitTest(const int tapX, const int tapY, const int areaX, const int areaY, const int areaW,
                          const int areaH, const int rowHeightOverride) const {
  const int font = systemFontId();
  const int row = rowHeightOverride > 0 ? rowHeightOverride : rowHeight(areaH, renderer_.text.getLineHeight(font));
  const int contentX = areaX + kInnerPadding;
  const int contentRight = areaX + areaW - kInnerPadding;
  const int contentTop = areaY + kTopPadding;
  const int contentBottom = areaY + areaH - kInnerPadding;
  if (tapX < contentX || tapX >= contentRight || tapY < contentTop || tapY >= contentBottom) return -1;

  const int index = (tapY - contentTop) / (row + kRowGap);
  if (index < 0 || index >= kItemCount) return -1;
  const int rowY = contentTop + index * (row + kRowGap);
  if (tapY >= rowY + row) return -1;
  return index;
}

#include "RecentGrid.h"

#include <BitmapRender.h>
#include <GfxRenderer.h>
#include <ImageRender.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "state/RecentBooks.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "util/SdIoMutex.h"

namespace {

constexpr int kColumns = 3;
constexpr int kRows = 2;
constexpr int kPageSize = kColumns * kRows;
constexpr int kGapX = 20;
constexpr int kGapY = 20;
constexpr int kMarginX = 20;
constexpr int kMarginY = 10;
constexpr int kCoverAspectWidth = 170;
constexpr int kCoverAspectHeight = 250;
constexpr int kFont = MONTSERRAT_12_FONT_ID;
constexpr int kCoverToTitleGap = 8;
constexpr int kTitleToPercentageGap = 5;

struct Geometry {
  int startY;
  int contentWidth;
  int cellWidth;
  int cellHeight;
  int lineHeight;
  int startIndex;
};

std::string thumbnailPath(const std::string& cacheDir) {
  if (cacheDir.empty()) return {};
  SdIoMutex::Lock ioLock;
  char path[192];
  for (const char* extension : {"thumb.jpg", "thumb.png", "thumb.bmp"}) {
    std::snprintf(path, sizeof(path), "%s/%s", cacheDir.c_str(), extension);
    if (SdMan.exists(path)) return path;
  }
  return {};
}

std::string titleFor(const RecentBook& book) {
  if (!book.title.empty()) return book.title;
  const size_t slash = book.path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = book.path.find_last_of('.');
  const size_t end = dot == std::string::npos || dot < start ? book.path.size() : dot;
  return book.path.substr(start, end - start);
}

Geometry geometry(const int x, const int y, const int width, const int height, const int pageIndex,
                  const int lineHeight) {
  const int contentWidth = std::max(1, width - kMarginX * 2);
  const int contentHeight = std::max(1, height - kMarginY * 2);
  const int cellWidth = std::max(1, (contentWidth - kGapX * (kColumns - 1)) / kColumns);
  const int cellHeight = std::max(1, (contentHeight - kGapY) / kRows);
  return {y + kMarginY, contentWidth, cellWidth, cellHeight, lineHeight,
          std::max(0, pageIndex) * kPageSize};
}

int cellX(const Geometry& g, const int x, const int column) {
  const int extraSpace = g.contentWidth - g.cellWidth * kColumns;
  return x + kMarginX + column * g.cellWidth + column * extraSpace / (kColumns - 1);
}

void coverRect(const Geometry& g, const int x, const int column, const int row, int& coverX, int& coverY,
               int& coverW, int& coverH, int& labelY) {
  const int cellY = g.startY + row * (g.cellHeight + kGapY);
  const int labelBlockHeight = kCoverToTitleGap + g.lineHeight + kTitleToPercentageGap + g.lineHeight;
  coverH = std::max(1, std::min(g.cellHeight - labelBlockHeight, g.cellWidth * kCoverAspectHeight / kCoverAspectWidth));
  coverW = std::max(1, coverH * kCoverAspectWidth / kCoverAspectHeight);
  coverX = cellX(g, x, column) + (g.cellWidth - coverW) / 2;
  const int cardHeight = coverH + labelBlockHeight;
  coverY = cellY + std::max(0, (g.cellHeight - cardHeight) / 2);
  labelY = coverY + coverH + kCoverToTitleGap;
}

void drawThumbnail(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                   const int height) {
  renderer.rectangle.fill(x, y, width, height, false);
  const std::string path = thumbnailPath(book.cachePath);
  if (!path.empty()) {
    ImageRender::Options options;
    options.cropToFill = true;
    options.useDisplayCache = true;
    options.asyncDisplayCache = true;
    if (SETTINGS.bitmapRoundedCorners == 0) {
      options.roundedOutside = BitmapRender::RoundedOutside::None;
    } else if (SETTINGS.bitmapRoundedCorners == 2) {
      options.roundedOutside = BitmapRender::RoundedOutside::SubtlePaperOutside;
    } else {
      options.roundedOutside = BitmapRender::RoundedOutside::PaperOutside;
    }
    if (ImageRender::create(renderer, path).render(x, y, width, height, options)) return;
  }
  renderer.rectangle.render(x, y, width, height, true, SETTINGS.bitmapRoundedCorners != 0,
                           SETTINGS.bitmapRoundedCorners == 2);
}

void renderPercentage(const GfxRenderer& renderer, const int x, const int y, const int width, float progress) {
  if (progress < 0.0f || progress > 1.0f) progress = 0.0f;
  char percent[8];
  std::snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(progress * 100.0f + 0.5f));
  const std::string shown = renderer.text.truncate(kFont, percent, std::max(1, width));
  renderer.text.renderGray(kFont, x, y, shown.c_str(), true);
}

void renderTitle(const GfxRenderer& renderer, const std::string& title, const int x, const int y, const int width) {
  const std::string shown = renderer.text.truncate(kFont, title.c_str(), std::max(1, width), EpdFontFamily::BOLD);
  renderer.text.render(kFont, x, y, shown.c_str(), true, EpdFontFamily::BOLD);
}

void mockCard(const GfxRenderer& renderer, const char* title, const int index, const Geometry& g, const int x) {
  int coverX, coverY, coverW, coverH, labelY;
  coverRect(g, x, index % kColumns, index / kColumns, coverX, coverY, coverW, coverH, labelY);
  renderer.rectangle.fill(coverX, coverY, coverW, coverH, false);
  renderer.rectangle.render(coverX, coverY, coverW, coverH, true);
  renderTitle(renderer, title, coverX, labelY, coverW);
  renderPercentage(renderer, coverX, labelY + renderer.text.getLineHeight(kFont) + kTitleToPercentageGap, coverW,
                   index == 1 ? 0.12f : index == 2 ? 0.0f : 0.42f);
}

}  // namespace

void RecentGrid::render(const int x, const int y, const int width, const int height, const int pageIndex) const {
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty()) {
    renderer_.text.centered(MONTSERRAT_12_FONT_ID, y + height / 2, "No recent books");
    return;
  }
  const int lastPage = std::max(0, (static_cast<int>(books.size()) - 1) / kPageSize);
  const Geometry g = geometry(x, y, width, height, std::min(std::max(0, pageIndex), lastPage),
                              renderer_.text.getLineHeight(kFont));
  for (int offset = 0; offset < kPageSize; ++offset) {
    const int index = g.startIndex + offset;
    if (index >= static_cast<int>(books.size())) break;
    int coverX, coverY, coverW, coverH, labelY;
    coverRect(g, x, offset % kColumns, offset / kColumns, coverX, coverY, coverW, coverH, labelY);
    drawThumbnail(renderer_, books[static_cast<size_t>(index)], coverX, coverY, coverW, coverH);
    renderer_.rectangle.render(coverX, coverY, coverW, coverH, true, SETTINGS.bitmapRoundedCorners != 0,
                               SETTINGS.bitmapRoundedCorners == 2);
    renderTitle(renderer_, titleFor(books[static_cast<size_t>(index)]), coverX, labelY, coverW);
    renderPercentage(renderer_, coverX, labelY + renderer_.text.getLineHeight(kFont) + kTitleToPercentageGap,
                     coverW, books[static_cast<size_t>(index)].progress);
  }
}

void RecentGrid::preview(const int x, const int y, const int width, const int height) const {
  static constexpr const char* titles[] = {"As a Man Thinketh", "Boyfriend", "Northern Tales",
                                           "The Beautiful Mind", "The Handbook", "The Modern Reader"};
  const Geometry g = geometry(x, y, width, height, 0, renderer_.text.getLineHeight(kFont));
  for (int index = 0; index < kPageSize; ++index) mockCard(renderer_, titles[index], index, g, x);
}

int RecentGrid::hitTest(const int x, const int y, const int width, const int height, const int pageIndex,
                        const int tapX, const int tapY) const {
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty()) return -1;
  const int lastPage = std::max(0, (static_cast<int>(books.size()) - 1) / kPageSize);
  const Geometry g = geometry(x, y, width, height, std::min(std::max(0, pageIndex), lastPage),
                              renderer_.text.getLineHeight(kFont));
  for (int offset = 0; offset < kPageSize; ++offset) {
    const int index = g.startIndex + offset;
    if (index >= static_cast<int>(books.size())) break;
    int coverX, coverY, coverW, coverH, labelY;
    coverRect(g, x, offset % kColumns, offset / kColumns, coverX, coverY, coverW, coverH, labelY);
    const int bottom = labelY + 2 * renderer_.text.getLineHeight(kFont);
    if (tapX >= coverX && tapX < coverX + coverW && tapY >= coverY && tapY < bottom) return index;
  }
  return -1;
}

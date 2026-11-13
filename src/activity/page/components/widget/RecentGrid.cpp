#include "RecentGrid.h"

#include <BitmapRender.h>
#include <GfxRenderer.h>
#include <ImageRender.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include "state/RecentBooks.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "BaseCarousel.h"

namespace {

constexpr int kColumns = 3;
constexpr int kRows = 2;
constexpr int kPageSize = RecentGrid::kPageSize;
constexpr int kGapY = 25;
constexpr int kMarginX = 20;
constexpr int kMarginY = 15;
int gridFont() { return systemFontIdForPointSize(12); }
constexpr int kGridThumbnailWidth = 118;
constexpr int kGridThumbnailHeight = 180;
constexpr int kFeaturedRowHeight = 200;
constexpr int kFeaturedThumbnailWidth = kGridThumbnailWidth;
constexpr int kFeaturedThumbnailHeight = kGridThumbnailHeight;
constexpr int kFeaturedSideMargin = 20;
constexpr int kFeaturedTextGap = 14;
int featuredTitleFont() { return systemFontIdForPointSize(14); }
int featuredDescriptionFont() { return systemFontIdForPointSize(10); }
int featuredProgressFont() { return systemFontIdForPointSize(18); }
constexpr int kFeaturedProgressBarHeight = 6;
constexpr int kFeaturedProgressBarGap = 8;
constexpr int kTitleToPercentageGap = 5;

struct Geometry {
  int startY;
  int contentWidth;
  int cellHeight;
  int lineHeight;
  int startIndex;
};

std::string thumbnailPath(const RecentBook& book) {
  return book.thumbnailPath;
}

std::string titleFor(const RecentBook& book) {
  if (!book.title.empty()) return book.title;
  const size_t slash = book.path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = book.path.find_last_of('.');
  const size_t end = dot == std::string::npos || dot < start ? book.path.size() : dot;
  return book.path.substr(start, end - start);
}

std::string plainDescription(const std::string& raw) {
  std::string output;
  output.reserve(raw.size());
  bool inTag = false;
  bool previousSpace = false;
  for (const char character : raw) {
    if (character == '<') {
      inTag = true;
      continue;
    }
    if (character == '>') {
      inTag = false;
      continue;
    }
    if (inTag) continue;
    const bool space = std::isspace(static_cast<unsigned char>(character)) != 0;
    if (space) {
      if (!previousSpace) output.push_back(' ');
    } else {
      output.push_back(character);
    }
    previousSpace = space;
  }
  while (!output.empty() && output.back() == ' ') output.pop_back();
  return output;
}

Geometry geometry(const int x, const int y, const int width, const int height, const int pageIndex,
                  const int lineHeight) {
  const int contentWidth = std::max(1, width - kMarginX * 2);
  const int contentHeight = std::max(1, height - kMarginY * 2);
  const int cellHeight = std::max(1, (contentHeight - kGapY) / kRows);
  return {y + kMarginY, contentWidth, cellHeight, lineHeight,
          std::max(0, pageIndex) * kPageSize};
}

void coverRect(const Geometry& g, const int x, const int column, const int row, int& coverX, int& coverY,
               int& coverW, int& coverH, int& labelY) {
  const int cellY = g.startY + row * (g.cellHeight + kGapY);
  const int labelBlockHeight = kTitleToPercentageGap + g.lineHeight;
  coverW = kGridThumbnailWidth;
  coverH = kGridThumbnailHeight;
  const int availableGapWidth = std::max(0, g.contentWidth - kColumns * coverW);
  const int equalGap = kColumns > 1 ? availableGapWidth / (kColumns - 1) : 0;
  coverX = x + kMarginX + column * (coverW + equalGap);
  const int cardHeight = coverH + labelBlockHeight;
  coverY = cellY + std::max(0, (g.cellHeight - cardHeight) / 2);
  labelY = coverY + coverH;
}

void drawThumbnail(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                   const int height) {
  // Match the Recent/Carousel widgets: draw a gray offset shadow underneath the cover,
  // rather than outlining the cover itself.
  BaseCarousel::renderShadow(renderer, x + 4, y + 4, width, height, HomeTheme::CarouselShadowStyle::Gray);
  renderer.rectangle.fill(x, y, width, height, false);
  const std::string path = thumbnailPath(book);
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
  const std::string shown = renderer.text.truncate(gridFont(), percent, std::max(1, width));
  renderer.text.renderGray(gridFont(), x, y, shown.c_str(), true);
}

void descriptionLines(const GfxRenderer& renderer, const std::string& value, const int width, std::string& first,
                      std::string& second) {
  first.clear();
  second.clear();
  if (value.empty()) return;
  const int font = featuredDescriptionFont();
  if (renderer.text.getWidth(font, value.c_str()) <= width) {
    first = value;
    return;
  }

  size_t split = value.find(' ');
  size_t best = std::string::npos;
  while (split != std::string::npos) {
    if (renderer.text.getWidth(font, value.substr(0, split).c_str()) > width) break;
    best = split;
    split = value.find(' ', split + 1);
  }

  if (best == std::string::npos) {
    first = renderer.text.truncate(font, value.c_str(), width);
    return;
  }

  first = value.substr(0, best);
  while (best < value.size() && value[best] == ' ') ++best;
  if (best < value.size()) second = renderer.text.truncate(font, value.substr(best).c_str(), width);
}

void renderFeaturedBook(GfxRenderer& renderer, const RecentBook& book, const std::string& description,
                        const int x, const int y, const int width) {
  constexpr int rowHeight = kFeaturedRowHeight;
  constexpr int thumbnailWidth = kFeaturedThumbnailWidth;
  constexpr int thumbnailHeight = kFeaturedThumbnailHeight;
  constexpr int sideMargin = kFeaturedSideMargin;
  constexpr int textGap = kFeaturedTextGap;
  const int textX = x + sideMargin + thumbnailWidth + textGap;
  const int textWidth = std::max(40, x + width - sideMargin - textX);
  const int thumbY = y + (rowHeight - thumbnailHeight) / 2;

  const int thumbnailX = x + sideMargin;
  drawThumbnail(renderer, book, thumbnailX, thumbY, thumbnailWidth, thumbnailHeight);
  renderer.rectangle.render(thumbnailX, thumbY, thumbnailWidth, thumbnailHeight, true,
                            SETTINGS.bitmapRoundedCorners != 0, SETTINGS.bitmapRoundedCorners == 2);

  const int titleY = y + 28;
  const int titleLine = renderer.text.getLineHeight(featuredTitleFont());
  const std::string title = renderer.text.truncate(featuredTitleFont(), titleFor(book).c_str(), textWidth,
                                                   EpdFontFamily::BOLD);
  renderer.text.render(featuredTitleFont(), textX, titleY, title.c_str(), true, EpdFontFamily::BOLD);

  const int descriptionY = titleY + titleLine + 12;
  std::string descriptionFirst;
  std::string descriptionSecond;
  descriptionLines(renderer, description, textWidth, descriptionFirst, descriptionSecond);
  renderer.text.render(featuredDescriptionFont(), textX, descriptionY, descriptionFirst.c_str(), true);
  if (!descriptionSecond.empty()) {
    renderer.text.render(featuredDescriptionFont(), textX,
                         descriptionY + renderer.text.getLineHeight(featuredDescriptionFont()),
                         descriptionSecond.c_str(), true);
  }
  const int progress = book.progress < 0.0f
                           ? 0
                           : std::max(0, std::min(100, static_cast<int>(book.progress * 100.0f + 0.5f)));
  const std::string progressText = std::to_string(progress) + "%";
  const int progressActualWidth = renderer.text.getWidth(featuredProgressFont(), progressText.c_str());
  const int progressLineHeight = renderer.text.getLineHeight(featuredProgressFont());
  const int progressY = y + rowHeight - 14 - progressLineHeight;

  const int progressX = x + width - sideMargin - progressActualWidth;
  const int progressBarWidth = std::max(1, progressX - kFeaturedProgressBarGap - textX);
  const int progressBarY = progressY + (progressLineHeight - kFeaturedProgressBarHeight) / 2;
  renderer.rectangle.fill(textX, progressBarY, progressBarWidth, kFeaturedProgressBarHeight,
                          static_cast<int>(GfxRenderer::FillTone::Gray), false);
  const int progressWidth = progressBarWidth * progress / 100;
  if (progressWidth > 0) {
    renderer.rectangle.fill(textX, progressBarY, progressWidth, kFeaturedProgressBarHeight,
                            static_cast<int>(GfxRenderer::FillTone::Ink), false);
  }
  renderer.rectangle.dotted(textX, progressBarY, progressBarWidth, kFeaturedProgressBarHeight, true);

  renderer.text.renderGray(featuredProgressFont(), progressX, progressY, progressText.c_str(), true,
                           EpdFontFamily::REGULAR);
  renderer.line.render(x + sideMargin, y + rowHeight - 1, x + width - sideMargin, y + rowHeight - 1, true,
                       LineRender::Style::Dotted);
}

void mockCard(const GfxRenderer& renderer, const int index, const Geometry& g, const int x) {
  int coverX, coverY, coverW, coverH, labelY;
  coverRect(g, x, index % kColumns, index / kColumns, coverX, coverY, coverW, coverH, labelY);
  renderer.rectangle.fill(coverX, coverY, coverW, coverH, false);
  renderer.rectangle.render(coverX, coverY, coverW, coverH, true);
  renderPercentage(renderer, coverX, labelY + kTitleToPercentageGap, coverW,
                   index == 1 ? 0.12f : index == 2 ? 0.0f : 0.42f);
}

}  // namespace

void RecentGrid::render(const int x, const int y, const int width, const int height, const int pageIndex) const {
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty()) {
    renderer_.text.centered(systemFontIdForPointSize(12), y + height / 2, "No recent books");
    return;
  }
  renderFeaturedBook(renderer_, books.front(), featuredDescription(books.front()), x, y, width);

  const int gridY = y + kFeaturedRowHeight;
  const int gridHeight = std::max(1, height - kFeaturedRowHeight);
  const int remainingBookCount = std::max(0, static_cast<int>(books.size()) - 1);
  const int lastPage = remainingBookCount == 0 ? 0 : (remainingBookCount - 1) / kPageSize;
  const Geometry g = geometry(x, gridY, width, gridHeight, std::min(std::max(0, pageIndex), lastPage),
                              renderer_.text.getLineHeight(gridFont()));
  const int startIndex = 1 + g.startIndex;
  for (int offset = 0; offset < kPageSize; ++offset) {
    const int index = startIndex + offset;
    if (index >= static_cast<int>(books.size())) break;
    int coverX, coverY, coverW, coverH, labelY;
    coverRect(g, x, offset % kColumns, offset / kColumns, coverX, coverY, coverW, coverH, labelY);
    drawThumbnail(renderer_, books[static_cast<size_t>(index)], coverX, coverY, coverW, coverH);
    renderer_.rectangle.render(coverX, coverY, coverW, coverH, true, SETTINGS.bitmapRoundedCorners != 0,
                               SETTINGS.bitmapRoundedCorners == 2);
    renderPercentage(renderer_, coverX, labelY + kTitleToPercentageGap,
                     coverW, books[static_cast<size_t>(index)].progress);
  }
}

std::string RecentGrid::featuredDescription(const RecentBook& book) const {
  if (featuredDescriptionPath_ == book.path) return featuredDescription_;

  featuredDescriptionPath_ = book.path;
  featuredDescription_ = plainDescription(book.description);
  if (featuredDescription_.empty()) {
    featuredDescription_ = book.author.empty() ? "-" : book.author;
  }
  return featuredDescription_;
}

void RecentGrid::preview(const int x, const int y, const int width, const int height) const {
  const RecentBook featured("", "", "Recently opened", "Author", 0.65f);
  renderFeaturedBook(renderer_, featured, "Description", x, y, width);
  const int gridY = y + kFeaturedRowHeight;
  const int gridHeight = std::max(1, height - kFeaturedRowHeight);
  const Geometry g = geometry(x, gridY, width, gridHeight, 0, renderer_.text.getLineHeight(gridFont()));
  for (int index = 0; index < kPageSize; ++index) mockCard(renderer_, index, g, x);
}

int RecentGrid::hitTest(const int x, const int y, const int width, const int height, const int pageIndex,
                        const int tapX, const int tapY) const {
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty()) return -1;
  if (tapX >= x && tapX < x + width && tapY >= y && tapY < y + kFeaturedRowHeight) return 0;

  const int gridY = y + kFeaturedRowHeight;
  const int gridHeight = std::max(1, height - kFeaturedRowHeight);
  const int remainingBookCount = std::max(0, static_cast<int>(books.size()) - 1);
  const int lastPage = remainingBookCount == 0 ? 0 : (remainingBookCount - 1) / kPageSize;
  const Geometry g = geometry(x, gridY, width, gridHeight, std::min(std::max(0, pageIndex), lastPage),
                              renderer_.text.getLineHeight(gridFont()));
  const int startIndex = 1 + g.startIndex;
  for (int offset = 0; offset < kPageSize; ++offset) {
    const int index = startIndex + offset;
    if (index >= static_cast<int>(books.size())) break;
    int coverX, coverY, coverW, coverH, labelY;
    coverRect(g, x, offset % kColumns, offset / kColumns, coverX, coverY, coverW, coverH, labelY);
    const int bottom = labelY + kTitleToPercentageGap + renderer_.text.getLineHeight(gridFont());
    if (tapX >= coverX && tapX < coverX + coverW && tapY >= coverY && tapY < bottom) return index;
  }
  return -1;
}

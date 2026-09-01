#include "Recent.h"

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

std::string thumbnailPath(const std::string& cacheDir) {
  if (cacheDir.empty()) return {};
  SdIoMutex::Lock ioLock;
  char path[192];
  for (const char* extension : {"thumb.jpg", "thumb.png", "thumb.bmp"}) {
    snprintf(path, sizeof(path), "%s/%s", cacheDir.c_str(), extension);
    if (SdMan.exists(path)) return path;
  }
  return {};
}

std::string bookTitle(const RecentBook& book) {
  if (!book.title.empty()) return book.title;
  const size_t slash = book.path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = book.path.find_last_of('.');
  const size_t end = dot == std::string::npos || dot < start ? book.path.size() : dot;
  return book.path.substr(start, end - start);
}

void titleLines(const GfxRenderer& renderer, const std::string& value, const int font, const int width,
                std::string& first, std::string& second) {
  first.clear();
  second.clear();
  if (value.empty()) return;
  if (renderer.text.getWidth(font, value.c_str(), EpdFontFamily::BOLD) <= width) {
    first = value;
    return;
  }

  size_t split = value.find(' ');
  size_t best = std::string::npos;
  while (split != std::string::npos) {
    if (renderer.text.getWidth(font, value.substr(0, split).c_str(), EpdFontFamily::BOLD) > width) break;
    best = split;
    split = value.find(' ', split + 1);
  }

  if (best == std::string::npos) {
    first = renderer.text.truncate(font, value.c_str(), width, EpdFontFamily::BOLD);
    return;
  }
  first = value.substr(0, best);
  while (best < value.size() && value[best] == ' ') ++best;
  second = renderer.text.truncate(font, value.substr(best).c_str(), width, EpdFontFamily::BOLD);
}

void thumbnailDimensions(const int width, const int height, int& thumbnailWidth, int& thumbnailHeight) {
  const bool wideLayout = width >= 360;
  const int maxHeight = wideLayout ? 300 : 230;
  const int maxWidth = wideLayout ? 200 : 154;
  thumbnailHeight = std::min(maxHeight, std::max(40, height - 32));
  thumbnailWidth = std::min(maxWidth, std::max(40, thumbnailHeight * 2 / 3));
}

void renderThumbnail(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                     const int height, const HomeTheme::CarouselShadowStyle shadowStyle) {
  BaseCarousel::renderShadow(renderer, x + 6, y + 6, width, height, shadowStyle);
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
      options.roundedOutside = BitmapRender::RoundedOutside::SubtleSparseInkAlignedOutside;
    } else {
      options.roundedOutside = BitmapRender::RoundedOutside::SparseInkAlignedOutside;
    }
    if (ImageRender::create(renderer, path).render(x, y, width, height, options)) return;
  }
  renderer.rectangle.render(x, y, width, height, true, SETTINGS.bitmapRoundedCorners != 0,
                           SETTINGS.bitmapRoundedCorners == 2);
}

}

void Recent::render(const int x, const int y, const int width, const int height, const bool background,
                    const HomeTheme::CarouselStyle style, const bool showLabel,
                    const HomeTheme::CarouselLabelColor labelColor,
                    const HomeTheme::CarouselShadowStyle shadowStyle) const {
  if (width <= 0 || height <= 0) return;
  renderBackground(x, y, width, height, background);
  const ContentArea content = contentArea(y, height, showLabel);
  if (showLabel) renderLabel(x, y, "Recent", labelColor);
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty()) return;

  const RecentBook& book = books.front();
  constexpr int innerPadding = 20;
  int thumbnailWidth = 0;
  int thumbnailHeight = 0;
  thumbnailDimensions(width, content.height, thumbnailWidth, thumbnailHeight);
  const bool rightAligned = style == HomeTheme::CarouselStyle::Right;
  const int thumbnailX = rightAligned ? x + width - innerPadding - thumbnailWidth : x + innerPadding;
  const int thumbnailY = content.y + (content.height - thumbnailHeight) / 2;
  renderThumbnail(renderer_, book, thumbnailX, thumbnailY, thumbnailWidth, thumbnailHeight, shadowStyle);
  renderer_.rectangle.render(thumbnailX, thumbnailY, thumbnailWidth, thumbnailHeight, true,
                             SETTINGS.bitmapRoundedCorners != 0, SETTINGS.bitmapRoundedCorners == 2);

  const int contentX = rightAligned ? x + innerPadding : thumbnailX + thumbnailWidth + 18;
  const int contentWidth = std::max(1, rightAligned ? thumbnailX - contentX - 18 : x + width - contentX - innerPadding);
  const int font = systemFontId();
  std::string title;
  std::string titleSecond;
  titleLines(renderer_, bookTitle(book), font, contentWidth, title, titleSecond);
  const std::string author = renderer_.text.truncate(font, book.author.c_str(), contentWidth);
  const int lineHeight = renderer_.text.getLineHeight(font);
  const int titleLines = titleSecond.empty() ? 1 : 2;
  const int titleBlockHeight = titleLines * lineHeight + (book.author.empty() ? 0 : lineHeight + 4);
  const int titleY = content.y + std::max(8, (content.height - titleBlockHeight) / 2 - 20);
  renderer_.text.render(font, contentX, titleY, title.c_str(), true, EpdFontFamily::BOLD);
  if (!titleSecond.empty()) {
    renderer_.text.render(font, contentX, titleY + lineHeight, titleSecond.c_str(), true, EpdFontFamily::BOLD);
  }
  if (!book.author.empty()) {
    const int authorY = titleY + titleLines * lineHeight + 4;
    renderer_.text.render(font, contentX, authorY, author.c_str(), true, EpdFontFamily::REGULAR);
  }

  const int percentage = book.progress < 0.0f ? 0 : std::max(0, std::min(100, static_cast<int>(book.progress * 100.0f + 0.5f)));
  const std::string percentageText = std::to_string(percentage) + "%";
  constexpr int percentageFont = MONTSERRAT_8_FONT_ID;
  const int percentageWidth = renderer_.text.getWidth(percentageFont, percentageText.c_str());
  const int barY = std::min(content.y + content.height - innerPadding - 11,
                            content.y + content.height / 2 + lineHeight + 20);
  const int barWidth = std::max(1, contentWidth - percentageWidth - 8);
  constexpr int barHeight = 5;
  constexpr int barInnerHeight = barHeight - 2;
  renderer_.rectangle.render(contentX, barY, barWidth, barHeight, true);
  renderer_.rectangle.fill(contentX + 1, barY + 1, std::max(1, barWidth - 2), barInnerHeight, false);
  if (percentage > 0) {
    renderer_.rectangle.fill(contentX + 1, barY + 1, std::max(1, (barWidth - 2) * percentage / 100),
                             barInnerHeight, true);
  }
  const int percentageY = barY + (barHeight - renderer_.text.getLineHeight(percentageFont)) / 2;
  renderer_.text.render(percentageFont, contentX + barWidth + 8, percentageY, percentageText.c_str(), true);
}

void Recent::preview(const int x, const int y, const int width, const int height, const bool background,
                     const HomeTheme::CarouselStyle style, const bool showLabel,
                     const HomeTheme::CarouselLabelColor labelColor,
                     const HomeTheme::CarouselShadowStyle shadowStyle) const {
  if (width <= 0 || height <= 0) return;
  renderBackground(x, y, width, height, background);
  const ContentArea content = contentArea(y, height, showLabel);
  if (showLabel) renderLabel(x, y, "Recent", labelColor);
  constexpr int innerPadding = 20;
  int thumbnailWidth = 0;
  int thumbnailHeight = 0;
  thumbnailDimensions(width, content.height, thumbnailWidth, thumbnailHeight);
  const bool rightAligned = style == HomeTheme::CarouselStyle::Right;
  const int thumbnailX = rightAligned ? x + width - innerPadding - thumbnailWidth : x + innerPadding;
  const int thumbnailY = content.y + (content.height - thumbnailHeight) / 2;

  BaseCarousel::renderShadow(renderer_, thumbnailX + 6, thumbnailY + 6, thumbnailWidth, thumbnailHeight, shadowStyle);
  renderer_.rectangle.fill(thumbnailX, thumbnailY, thumbnailWidth, thumbnailHeight, false);
  renderer_.rectangle.render(thumbnailX, thumbnailY, thumbnailWidth, thumbnailHeight, true,
                             SETTINGS.bitmapRoundedCorners != 0, SETTINGS.bitmapRoundedCorners == 2);

  const int contentX = rightAligned ? x + innerPadding : thumbnailX + thumbnailWidth + 18;
  const int contentWidth = std::max(1, rightAligned ? thumbnailX - contentX - 18 : x + width - contentX - innerPadding);
  const int font = systemFontId();
  const int lineHeight = renderer_.text.getLineHeight(font);
  const int titleY = content.y + std::max(8, (content.height - lineHeight * 2 - 28) / 2 - 20);
  renderer_.text.render(font, contentX, titleY, "Book title", true, EpdFontFamily::BOLD);
  const int authorY = titleY + lineHeight + 4;
  renderer_.text.render(font, contentX, authorY, "Author", true, EpdFontFamily::REGULAR);

  constexpr int percentage = 65;
  const int percentageWidth = renderer_.text.getWidth(MONTSERRAT_8_FONT_ID, "65%");
  const int barY = std::min(content.y + content.height - innerPadding - 11,
                            content.y + content.height / 2 + lineHeight + 20);
  const int barWidth = std::max(1, contentWidth - percentageWidth - 8);
  constexpr int barHeight = 5;
  constexpr int barInnerHeight = barHeight - 2;
  renderer_.rectangle.render(contentX, barY, barWidth, barHeight, true);
  renderer_.rectangle.fill(contentX + 1, barY + 1, std::max(1, barWidth - 2), barInnerHeight, false);
  renderer_.rectangle.fill(contentX + 1, barY + 1, std::max(1, (barWidth - 2) * percentage / 100),
                           barInnerHeight, true);
  constexpr int percentageFont = MONTSERRAT_8_FONT_ID;
  const int percentageY = barY + (barHeight - renderer_.text.getLineHeight(percentageFont)) / 2;
  renderer_.text.render(percentageFont, contentX + barWidth + 8, percentageY, "65%", true);
}

int Recent::hitTest(const int x, const int y, const int areaX, const int areaY, const int areaW, const int areaH) const {
  if (RECENT_BOOKS.getCount() == 0) return -1;
  return x >= areaX && x < areaX + areaW && y >= areaY && y < areaY + areaH ? 0 : -1;
}

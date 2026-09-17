#include "Carousel.h"

#include <BitmapRender.h>
#include <GfxRenderer.h>
#include <ImageRender.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "state/RecentBooks.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "util/SdIoMutex.h"

namespace {
constexpr int kLeftCardMargin = 20;
constexpr int kLeftCardGap = 20;

bool evenThumbnails() { return SETTINGS.thumbnailSize == SystemSetting::THUMBNAIL_EVEN; }

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

void renderCover(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                 const int height, const int font, const float cropAnchorX = 0.5f, const bool cropToFill = true,
                 const HomeTheme::CarouselShadowStyle shadowStyle = HomeTheme::CarouselShadowStyle::None) {
  const std::string path = thumbnailPath(book.cachePath);
  int backgroundX = x;
  int backgroundY = y;
  int backgroundWidth = width;
  int backgroundHeight = height;
  if (!path.empty() && !cropToFill) {
    int sourceWidth = 0;
    int sourceHeight = 0;
    if (ImageRender::getDimensions(path, &sourceWidth, &sourceHeight) && sourceWidth > 0 && sourceHeight > 0) {
      const float scale = std::min(static_cast<float>(width) / static_cast<float>(sourceWidth),
                                   static_cast<float>(height) / static_cast<float>(sourceHeight));
      backgroundWidth = std::max(1, static_cast<int>(std::lround(sourceWidth * scale)));
      backgroundHeight = std::max(1, static_cast<int>(std::lround(sourceHeight * scale)));
      backgroundX = x + (width - backgroundWidth) / 2;
      backgroundY = y + (height - backgroundHeight) / 2;
    }
  }
  BaseCarousel::renderShadow(renderer, backgroundX + 6, backgroundY + 6, backgroundWidth, backgroundHeight,
                             shadowStyle);
  renderer.rectangle.fill(backgroundX, backgroundY, backgroundWidth, backgroundHeight, false);
  if (!path.empty()) {
    ImageRender::Options options;
    options.cropToFill = cropToFill;
    options.cropAnchorX = cropAnchorX;
    options.useDisplayCache = true;
    options.asyncDisplayCache = true;
    if (SETTINGS.bitmapRoundedCorners == 0) {
      options.roundedOutside = BitmapRender::RoundedOutside::None;
    } else if (SETTINGS.bitmapRoundedCorners == 2) {
      options.roundedOutside = BitmapRender::RoundedOutside::SubtleSparseInkAlignedOutside;
    } else {
      options.roundedOutside = BitmapRender::RoundedOutside::SparseInkAlignedOutside;
    }
    const int imageX = cropToFill ? x : backgroundX;
    const int imageY = cropToFill ? y : backgroundY;
    const int imageWidth = cropToFill ? width : backgroundWidth;
    const int imageHeight = cropToFill ? height : backgroundHeight;
    if (ImageRender::create(renderer, path).render(imageX, imageY, imageWidth, imageHeight, options)) {
      renderer.rectangle.render(backgroundX, backgroundY, backgroundWidth, backgroundHeight, true,
                                SETTINGS.bitmapRoundedCorners != 0, SETTINGS.bitmapRoundedCorners == 2);
      return;
    }
  }

  renderer.rectangle.fill(backgroundX, backgroundY, backgroundWidth, backgroundHeight, false);
  renderer.rectangle.render(backgroundX, backgroundY, backgroundWidth, backgroundHeight, true,
                            SETTINGS.bitmapRoundedCorners != 0, SETTINGS.bitmapRoundedCorners == 2);
  const std::string text = bookTitle(book);
  const int maxTextWidth = std::max(1, backgroundWidth - 8);
  const std::string shown = renderer.text.truncate(font, text.c_str(), maxTextWidth, EpdFontFamily::REGULAR);
  const int lineHeight = renderer.text.getLineHeight(font);
  const int textWidth = renderer.text.getWidth(font, shown.c_str());
  const int textX = backgroundX + std::max(4, (backgroundWidth - std::min(backgroundWidth - 8, textWidth)) / 2);
  renderer.text.render(font, textX, backgroundY + std::max(4, (backgroundHeight - lineHeight) / 2), shown.c_str(), true,
                       EpdFontFamily::REGULAR);
}

void renderProgressTag(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                       const int height) {
  if (width < 8 || height < 8) return;

  constexpr int paddingX = 6;
  constexpr int paddingY = 4;
  constexpr int margin = 5;
  constexpr int font = MONTSERRAT_8_FONT_ID;
  const int percentage =
      book.progress < 0.0f ? 0 : std::max(0, std::min(100, static_cast<int>(book.progress * 100.0f + 0.5f)));
  const std::string label = std::to_string(percentage) + "%";
  const int tagWidth = renderer.text.getWidth(font, label.c_str()) + paddingX * 2;
  const int tagHeight = renderer.text.getLineHeight(font) + paddingY * 2;
  const int tagX = x + std::max(0, width - tagWidth - margin);
  const int tagY = y + margin;
  renderer.rectangle.fill(tagX, tagY, tagWidth, tagHeight, static_cast<int>(GfxRenderer::FillTone::Ink), true);
  renderer.rectangle.render(tagX, tagY, tagWidth, tagHeight, false, true);
  renderer.text.render(font, tagX + paddingX, tagY + paddingY, label.c_str(), false);
}

void preloadCover(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                  const int height, const float cropAnchorX, const bool cropToFill = true) {
  const std::string path = thumbnailPath(book.cachePath);
  if (path.empty()) return;

  ImageRender::Options options;
  options.cropToFill = cropToFill;
  options.cropAnchorX = cropAnchorX;
  options.useDisplayCache = true;
  if (SETTINGS.bitmapRoundedCorners == 2) {
    options.roundedOutside = BitmapRender::RoundedOutside::SubtleSparseInkAlignedOutside;
  } else if (SETTINGS.bitmapRoundedCorners != 0) {
    options.roundedOutside = BitmapRender::RoundedOutside::SparseInkAlignedOutside;
  }
  ImageRender::create(renderer, path).preloadDisplayCache(x, y, width, height, options);
}

struct CarouselBounds {
  int centerX;
  int centerY;
  int centerWidth;
  int centerHeight;
  int sideX;
  int sideY;
  int sideWidth;
  int sideHeight;
  int gap;
};

struct LeftCardBounds {
  int x;
  int y;
  int width;
  int height;
};

size_t visibleBookCount(const std::vector<RecentBook>& books, const bool hideFirst) {
  return hideFirst && !books.empty() ? books.size() - 1 : books.size();
}

size_t visibleBookIndex(const std::vector<RecentBook>& books, const int index, const bool hideFirst) {
  const size_t first = hideFirst ? 1 : 0;
  const size_t count = visibleBookCount(books, hideFirst);
  if (count == 0) return 0;
  const int normalized = ((index % static_cast<int>(count)) + static_cast<int>(count)) % static_cast<int>(count);
  return first + static_cast<size_t>(normalized);
}

LeftCardBounds leftCardBounds(const RecentBook& book, const int cardX, const int y, const int width, const int height) {
  constexpr int horizontalPadding = kLeftCardMargin;
  constexpr int topPadding = 20;
  constexpr int bottomPadding = 20;
  constexpr int gap = kLeftCardGap;
  const int contentHeight = std::max(24, height - topPadding - bottomPadding);
  int sourceWidth = 2;
  int sourceHeight = 3;
  const std::string path = thumbnailPath(book.cachePath);
  if (!path.empty()) {
    int detectedWidth = 0;
    int detectedHeight = 0;
    if (ImageRender::getDimensions(path, &detectedWidth, &detectedHeight) && detectedWidth > 0 && detectedHeight > 0) {
      sourceWidth = detectedWidth;
      sourceHeight = detectedHeight;
    }
  }
  const int naturalWidth =
      std::max(24, static_cast<int>(std::lround(static_cast<float>(contentHeight) * sourceWidth / sourceHeight)));
  const int maxCardWidth = std::max(24, (width - horizontalPadding - gap * 2) * 9 / 20);
  const int cardWidth = std::min(naturalWidth, maxCardWidth);
  const int cardHeight =
      std::max(24, std::min(contentHeight,
                            static_cast<int>(std::lround(static_cast<float>(cardWidth) * sourceHeight / sourceWidth))));
  const int cardY = y + height - bottomPadding - cardHeight;
  return {cardX, cardY, cardWidth, cardHeight};
}

CarouselBounds bounds(const int areaX, const int areaY, const int areaW, const int areaH) {
  constexpr int minimumSideWidth = 24;
  const int gap = areaW >= 360 ? 12 : 6;
  int centerWidth = std::min(200, std::max(40, areaW * 3 / 5));
  int sideWidth = (areaW - centerWidth - gap * 2) / 2;
  if (sideWidth < minimumSideWidth) {
    centerWidth = std::max(40, areaW - gap * 2 - minimumSideWidth * 2);
    sideWidth = minimumSideWidth;
  }

  const int centerHeight = std::min(300, std::max(50, areaH - 28));
  const int centerX = areaX + (areaW - centerWidth) / 2;
  const int centerY = areaY + (areaH - centerHeight) / 2;
  const int sideHeight = std::max(32, centerHeight * 9 / 10);
  const int sideY = centerY + (centerHeight - sideHeight) / 2;
  return {centerX, centerY, centerWidth, centerHeight, centerX - sideWidth - gap, sideY, sideWidth, sideHeight, gap};
}

CarouselBounds twoBookBounds(const int areaX, const int areaY, const int areaW, const int areaH,
                             const RecentBook& centerBook, const RecentBook& sideBook) {
  constexpr int leftMargin = 20;
  const int gap = 20;
  int centerWidth = std::min(204, std::max(40, areaW * 3 / 5));
  const int centerHeight = std::min(312, std::max(50, areaH - 28));
  const std::string centerPath = thumbnailPath(centerBook.cachePath);
  if (!centerPath.empty()) {
    int sourceWidth = 0;
    int sourceHeight = 0;
    if (ImageRender::getDimensions(centerPath, &sourceWidth, &sourceHeight) && sourceWidth > 0 && sourceHeight > 0) {
      centerWidth =
          std::max(40, static_cast<int>(std::lround(static_cast<float>(centerHeight) * sourceWidth / sourceHeight)));
      centerWidth = std::min(204, centerWidth);
    }
  }
  const int sideHeight = std::max(32, centerHeight * 9 / 10);
  int sideWidth = std::max(24, centerWidth * 3 / 5);
  const std::string path = thumbnailPath(sideBook.cachePath);
  if (!path.empty()) {
    int sourceWidth = 0;
    int sourceHeight = 0;
    if (ImageRender::getDimensions(path, &sourceWidth, &sourceHeight) && sourceWidth > 0 && sourceHeight > 0) {
      sideWidth =
          std::max(24, static_cast<int>(std::lround(static_cast<float>(sideHeight) * sourceWidth / sourceHeight)));
    }
  }
  const int centerX = areaX + leftMargin;
  const int centerY = areaY + (areaH - centerHeight) / 2;
  const int sideY = centerY + centerHeight - sideHeight;
  return {centerX, centerY, centerWidth, centerHeight, centerX + centerWidth + gap, sideY, sideWidth, sideHeight, gap};
}

void preloadFrame(GfxRenderer& renderer, const std::vector<RecentBook>& books, const int current,
                  const CarouselBounds& layout, const bool hideFirst) {
  const bool even = evenThumbnails();
  const size_t count = visibleBookCount(books, hideFirst);
  const size_t currentBook = visibleBookIndex(books, current, hideFirst);
  if (count > 1) {
    preloadCover(renderer, books[visibleBookIndex(books, current - 1, hideFirst)], layout.sideX, layout.sideY,
                 layout.sideWidth, layout.sideHeight, 1.0f, true);
    preloadCover(renderer, books[visibleBookIndex(books, current + 1, hideFirst)],
                 layout.centerX + layout.centerWidth + layout.gap, layout.sideY, layout.sideWidth, layout.sideHeight,
                 0.0f, true);
  }
  preloadCover(renderer, books[currentBook], layout.centerX, layout.centerY, layout.centerWidth, layout.centerHeight,
               0.5f, even);
}

}  // namespace

void Carousel::render(const int index, const int x, const int y, const int width, const int height,
                      const bool background, const HomeTheme::CarouselStyle style, const bool showLabel,
                      const HomeTheme::CarouselLabelColor labelColor, const HomeTheme::CarouselShadowStyle shadowStyle,
                      const bool showProgress, const bool hideFirst) const {
  renderBackground(x, y, width, height, background);
  const ContentArea content = contentArea(y, height, showLabel);
  if (showLabel) renderLabel(x, y, "Continue Reading", labelColor);
  if (style == HomeTheme::CarouselStyle::Left) {
    renderLeft(index, x, content.y, width, content.height, shadowStyle, showProgress, hideFirst);
    return;
  }
  const auto& books = RECENT_BOOKS.getBooks();
  const size_t count = visibleBookCount(books, hideFirst);
  if (count == 0) {
    const int coverWidth = std::min(210, std::max(40, width - 30));
    const int coverHeight = std::min(318, std::max(40, height - 22));
    const int coverX = x + (width - coverWidth) / 2;
    const int coverY = y + (height - coverHeight) / 2;
    renderer_.rectangle.fill(coverX, coverY, coverWidth, coverHeight, false);
    renderer_.rectangle.render(coverX, coverY, coverWidth, coverHeight, true);
    renderer_.text.centered(MONTSERRAT_12_FONT_ID, content.y + content.height / 2, "No recent");
    return;
  }

  const CarouselBounds layout =
      count == 2 ? twoBookBounds(x, content.y, width, content.height, books[visibleBookIndex(books, 0, hideFirst)],
                                 books[visibleBookIndex(books, 1, hideFirst)])
                 : bounds(x, content.y, width, content.height);
  const int current = index;
  if (count == 2) {
    const bool even = evenThumbnails();
    renderCover(renderer_, books[visibleBookIndex(books, 0, hideFirst)], layout.centerX, layout.centerY,
                layout.centerWidth, layout.centerHeight, MONTSERRAT_14_FONT_ID, 0.5f, even, shadowStyle);
    if (showProgress)
      renderProgressTag(renderer_, books[visibleBookIndex(books, 0, hideFirst)], layout.centerX, layout.centerY,
                        layout.centerWidth, layout.centerHeight);
    renderCover(renderer_, books[visibleBookIndex(books, 1, hideFirst)], layout.sideX, layout.sideY, layout.sideWidth,
                layout.sideHeight, MONTSERRAT_10_FONT_ID, 0.0f, true, shadowStyle);
    if (showProgress)
      renderProgressTag(renderer_, books[visibleBookIndex(books, 1, hideFirst)], layout.sideX, layout.sideY,
                        layout.sideWidth, layout.sideHeight);
    return;
  }
  const bool even = evenThumbnails();
  if (count > 1) {
    renderCover(renderer_, books[visibleBookIndex(books, current - 1, hideFirst)], layout.sideX, layout.sideY,
                layout.sideWidth, layout.sideHeight, MONTSERRAT_10_FONT_ID, 1.0f, true, shadowStyle);
    if (showProgress) {
      const RecentBook& book = books[visibleBookIndex(books, current - 1, hideFirst)];
      renderProgressTag(renderer_, book, layout.sideX, layout.sideY, layout.sideWidth, layout.sideHeight);
    }
  }
  if (count > 1) {
    renderCover(renderer_, books[visibleBookIndex(books, current + 1, hideFirst)],
                layout.centerX + layout.centerWidth + layout.gap, layout.sideY, layout.sideWidth, layout.sideHeight,
                MONTSERRAT_10_FONT_ID, 0.0f, true, shadowStyle);
    if (showProgress) {
      const RecentBook& book = books[visibleBookIndex(books, current + 1, hideFirst)];
      renderProgressTag(renderer_, book, layout.centerX + layout.centerWidth + layout.gap, layout.sideY,
                        layout.sideWidth, layout.sideHeight);
    }
  }
  renderCover(renderer_, books[visibleBookIndex(books, current, hideFirst)], layout.centerX, layout.centerY,
              layout.centerWidth, layout.centerHeight, MONTSERRAT_14_FONT_ID, 0.5f, even, shadowStyle);
  if (showProgress) {
    renderProgressTag(renderer_, books[visibleBookIndex(books, current, hideFirst)], layout.centerX, layout.centerY,
                      layout.centerWidth, layout.centerHeight);
  }
}

void Carousel::renderLeft(const int index, const int x, const int y, const int width, const int height,
                          const HomeTheme::CarouselShadowStyle shadowStyle, const bool showProgress,
                          const bool hideFirst) const {
  const auto& books = RECENT_BOOKS.getBooks();
  const size_t count = visibleBookCount(books, hideFirst);
  if (count == 0) {
    renderer_.text.centered(systemFontId(), y + height / 2, "No recent");
    return;
  }

  const int current = ((index % static_cast<int>(count)) + static_cast<int>(count)) % static_cast<int>(count);
  const bool even = evenThumbnails();
  const int visible = std::min(4, static_cast<int>(count));
  int cardX = x + kLeftCardMargin;
  for (int offset = 0; offset < visible; ++offset) {
    const size_t bookIndex = visibleBookIndex(books, current + offset, hideFirst);
    const LeftCardBounds card = leftCardBounds(books[bookIndex], cardX, y, width, height);
    if (card.x >= x + width) break;
    const int visibleWidth = std::min(card.width, x + width - card.x);
    if (visibleWidth <= 0) break;
    const bool clipped = visibleWidth < card.width;
    renderCover(renderer_, books[bookIndex], card.x, card.y, visibleWidth, card.height, MONTSERRAT_10_FONT_ID,
                clipped ? 0.0f : 0.5f, even || clipped, shadowStyle);
    if (showProgress && !clipped) {
      renderProgressTag(renderer_, books[static_cast<size_t>(bookIndex)], card.x, card.y, visibleWidth, card.height);
    }
    cardX += card.width + kLeftCardGap;
  }
}

void Carousel::preload(const int index, const int x, const int y, const int width, const int height,
                       const HomeTheme::CarouselStyle style, const bool showLabel,
                       const HomeTheme::CarouselLabelColor /*labelColor*/, const bool hideFirst) const {
  const auto& books = RECENT_BOOKS.getBooks();
  const size_t count = visibleBookCount(books, hideFirst);
  if (count == 0 || width <= 0 || height <= 0) return;
  const ContentArea content = contentArea(y, height, showLabel);

  if (style == HomeTheme::CarouselStyle::Left) {
    const bool even = evenThumbnails();
    for (size_t current = 0; current < count; ++current) {
      int cardX = x + kLeftCardMargin;
      const int visible = std::min(4, static_cast<int>(count));
      for (int offset = 0; offset < visible; ++offset) {
        const size_t bookIndex = visibleBookIndex(books, static_cast<int>(current) + offset, hideFirst);
        const LeftCardBounds card = leftCardBounds(books[bookIndex], cardX, content.y, width, content.height);
        if (card.x >= x + width) break;
        const int visibleWidth = std::min(card.width, x + width - card.x);
        if (visibleWidth > 0) {
          const bool clipped = visibleWidth < card.width;
          preloadCover(renderer_, books[bookIndex], card.x, card.y, visibleWidth, card.height, clipped ? 0.0f : 0.5f,
                       even || clipped);
        }
        cardX += card.width + kLeftCardGap;
      }
    }
    return;
  }

  const CarouselBounds layout =
      count == 2 ? twoBookBounds(x, content.y, width, content.height, books[visibleBookIndex(books, 0, hideFirst)],
                                 books[visibleBookIndex(books, 1, hideFirst)])
                 : bounds(x, content.y, width, content.height);
  (void)index;
  if (count == 2) {
    const bool even = evenThumbnails();
    preloadCover(renderer_, books[visibleBookIndex(books, 0, hideFirst)], layout.centerX, layout.centerY,
                 layout.centerWidth, layout.centerHeight, 0.5f, even);
    preloadCover(renderer_, books[visibleBookIndex(books, 1, hideFirst)], layout.sideX, layout.sideY, layout.sideWidth,
                 layout.sideHeight, 0.0f, true);
    return;
  }
  for (size_t book = 0; book < count; ++book) {
    preloadFrame(renderer_, books, static_cast<int>(book), layout, hideFirst);
  }
}

void Carousel::preview(const int x, const int y, const int width, const int height, const bool background,
                       const HomeTheme::CarouselStyle style, const bool showLabel,
                       const HomeTheme::CarouselLabelColor labelColor, const HomeTheme::CarouselShadowStyle shadowStyle,
                       const bool showProgress) const {
  renderBackground(x, y, width, height, background);
  const ContentArea content = contentArea(y, height, showLabel);
  if (showLabel) renderLabel(x, y, "Continue Reading", labelColor);
  if (style == HomeTheme::CarouselStyle::Left) {
    previewLeft(x, content.y, width, content.height, shadowStyle, showProgress);
    return;
  }
  const CarouselBounds layout = bounds(x, content.y, width, content.height);

  auto renderCoverPlaceholder = [this, shadowStyle, showProgress](const int coverX, const int coverY,
                                                                  const int coverWidth, const int coverHeight) {
    renderShadow(renderer_, coverX + 6, coverY + 6, coverWidth, coverHeight, shadowStyle);
    renderer_.rectangle.fill(coverX, coverY, coverWidth, coverHeight, false);
    renderer_.rectangle.render(coverX, coverY, coverWidth, coverHeight, true, SETTINGS.bitmapRoundedCorners != 0,
                               SETTINGS.bitmapRoundedCorners == 2);
    if (showProgress) {
      const RecentBook placeholder("", "", "Book title", "Author", 0.65f);
      renderProgressTag(renderer_, placeholder, coverX, coverY, coverWidth, coverHeight);
    }
  };

  renderCoverPlaceholder(layout.sideX, layout.sideY, layout.sideWidth, layout.sideHeight);
  renderCoverPlaceholder(layout.centerX, layout.centerY, layout.centerWidth, layout.centerHeight);
  renderCoverPlaceholder(layout.centerX + layout.centerWidth + layout.gap, layout.sideY, layout.sideWidth,
                         layout.sideHeight);
}

void Carousel::previewLeft(const int x, const int y, const int width, const int height,
                           const HomeTheme::CarouselShadowStyle shadowStyle, const bool showProgress) const {
  constexpr int horizontalPadding = kLeftCardMargin;
  constexpr int topPadding = 20;
  constexpr int bottomPadding = 20;
  constexpr int gap = kLeftCardGap;
  const int cardHeight = std::max(24, height - topPadding - bottomPadding);
  const int naturalWidth = std::max(24, cardHeight * 2 / 3);
  const int maxCardWidth = std::max(24, (width - horizontalPadding - gap * 2) * 9 / 20);
  const int cardWidth = std::min(naturalWidth, maxCardWidth);
  for (int offset = 0; offset < 3; ++offset) {
    const int cardX = x + horizontalPadding + offset * (cardWidth + gap);
    if (cardX >= x + width) break;
    const int visibleWidth = std::min(cardWidth, x + width - cardX);
    const int cardY = y + height - bottomPadding - cardHeight;
    renderShadow(renderer_, cardX + 6, cardY + 6, visibleWidth, cardHeight, shadowStyle);
    renderer_.rectangle.fill(cardX, cardY, visibleWidth, cardHeight, false);
    renderer_.rectangle.render(cardX, cardY, visibleWidth, cardHeight, true, SETTINGS.bitmapRoundedCorners != 0,
                               SETTINGS.bitmapRoundedCorners == 2);
    if (showProgress && visibleWidth == cardWidth) {
      const RecentBook placeholder("", "", "Book title", "Author", 0.65f);
      renderProgressTag(renderer_, placeholder, cardX, cardY, visibleWidth, cardHeight);
    }
  }
}

int Carousel::hitTest(const int index, const int count, const int x, const int y, const int areaX, const int areaY,
                      const int areaW, const int areaH, const HomeTheme::CarouselStyle style, const bool showLabel,
                      const HomeTheme::CarouselLabelColor /*labelColor*/, const bool hideFirst) const {
  if (count <= 0 || x < areaX || x >= areaX + areaW || y < areaY || y >= areaY + areaH) return -1;
  const ContentArea content = contentArea(areaY, areaH, showLabel);
  if (style == HomeTheme::CarouselStyle::Left) {
    return hitTestLeft(index, count, x, y, areaX, content.y, areaW, content.height, hideFirst);
  }
  const auto& books = RECENT_BOOKS.getBooks();
  const int visibleCount = std::min(count, static_cast<int>(visibleBookCount(books, hideFirst)));
  if (visibleCount <= 0) return -1;
  const CarouselBounds layout = visibleCount == 2 ? twoBookBounds(areaX, content.y, areaW, content.height,
                                                                  books[visibleBookIndex(books, 0, hideFirst)],
                                                                  books[visibleBookIndex(books, 1, hideFirst)])
                                                  : bounds(areaX, content.y, areaW, content.height);
  if (visibleCount == 2) {
    if (x >= layout.centerX && x < layout.centerX + layout.centerWidth && y >= layout.centerY &&
        y < layout.centerY + layout.centerHeight) {
      return static_cast<int>(visibleBookIndex(books, 0, hideFirst));
    }
    if (x >= layout.sideX && x < layout.sideX + layout.sideWidth && y >= layout.sideY &&
        y < layout.sideY + layout.sideHeight) {
      return static_cast<int>(visibleBookIndex(books, 1, hideFirst));
    }
    return -1;
  }
  if (x >= layout.centerX && x < layout.centerX + layout.centerWidth && y >= layout.centerY &&
      y < layout.centerY + layout.centerHeight) {
    return static_cast<int>(visibleBookIndex(books, index, hideFirst));
  }

  if (visibleCount > 1 && x >= layout.sideX && x < layout.sideX + layout.sideWidth && y >= layout.sideY &&
      y < layout.sideY + layout.sideHeight) {
    return static_cast<int>(visibleBookIndex(books, index - 1, hideFirst));
  }
  const int rightX = layout.centerX + layout.centerWidth + layout.gap;
  if (visibleCount > 1 && x >= rightX && x < rightX + layout.sideWidth && y >= layout.sideY &&
      y < layout.sideY + layout.sideHeight) {
    return static_cast<int>(visibleBookIndex(books, index + 1, hideFirst));
  }
  return -1;
}

int Carousel::hitTestLeft(const int index, const int count, const int x, const int y, const int areaX, const int areaY,
                          const int areaW, const int areaH, const bool hideFirst) const {
  const auto& books = RECENT_BOOKS.getBooks();
  const int visibleCount = std::min(count, static_cast<int>(visibleBookCount(books, hideFirst)));
  if (visibleCount <= 0) return -1;
  const int current = ((index % visibleCount) + visibleCount) % visibleCount;
  const int visible = std::min(4, visibleCount);
  int cardX = areaX + kLeftCardMargin;
  for (int offset = 0; offset < visible; ++offset) {
    const size_t bookIndex = visibleBookIndex(books, current + offset, hideFirst);
    const LeftCardBounds card = leftCardBounds(books[bookIndex], cardX, areaY, areaW, areaH);
    if (x >= card.x && x < card.x + card.width && y >= card.y && y < card.y + card.height) {
      return static_cast<int>(bookIndex);
    }
    cardX += card.width + kLeftCardGap;
  }
  return -1;
}

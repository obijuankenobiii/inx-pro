#include "Favorites.h"

#include <BitmapRender.h>
#include <GfxRenderer.h>
#include <ImageRender.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>

#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "util/SdIoMutex.h"

namespace {
constexpr int kHorizontalPadding = 20;
constexpr int kTopPadding = 20;
constexpr int kBottomPadding = 20;
constexpr int kCardGap = 20;
constexpr int kFallbackSourceWidth = 2;
constexpr int kFallbackSourceHeight = 3;

std::string bookTitle(const BookState::Book& book) {
  if (!book.title.empty()) return book.title;
  const size_t slash = book.path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = book.path.find_last_of('.');
  const size_t end = dot == std::string::npos || dot < start ? book.path.size() : dot;
  return book.path.substr(start, end - start);
}

bool endsWith(const std::string& value, const char* suffix) {
  const size_t length = std::char_traits<char>::length(suffix);
  return value.size() >= length && value.compare(value.size() - length, length, suffix) == 0;
}

void setRoundedOptions(ImageRender::Options& options) {
  options.cropToFill = false;
  options.useDisplayCache = true;
  options.asyncDisplayCache = true;
  if (SETTINGS.bitmapRoundedCorners == 0) {
    options.roundedOutside = BitmapRender::RoundedOutside::None;
  } else if (SETTINGS.bitmapRoundedCorners == 2) {
    options.roundedOutside = BitmapRender::RoundedOutside::SubtleSparseInkAlignedOutside;
  } else {
    options.roundedOutside = BitmapRender::RoundedOutside::SparseInkAlignedOutside;
  }
}

struct CenteredCarouselLayout {
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

CenteredCarouselLayout centeredBounds(const int areaX, const int areaY, const int areaW, const int areaH) {
  constexpr int minimumSideWidth = 24;
  const int gap = areaW >= 360 ? 12 : 6;
  int centerWidth = std::min(204, std::max(40, areaW * 3 / 5));
  int sideWidth = (areaW - centerWidth - gap * 2) / 2;
  if (sideWidth < minimumSideWidth) {
    centerWidth = std::max(40, areaW - gap * 2 - minimumSideWidth * 2);
    sideWidth = minimumSideWidth;
  }
  const int centerHeight = std::min(312, std::max(50, areaH - 28));
  const int centerX = areaX + (areaW - centerWidth) / 2;
  const int centerY = areaY + (areaH - centerHeight) / 2;
  const int sideHeight = std::max(32, centerHeight * 9 / 10);
  const int sideY = centerY + (centerHeight - sideHeight) / 2;
  return {centerX, centerY, centerWidth, centerHeight, centerX - sideWidth - gap, sideY, sideWidth, sideHeight, gap};
}
}

void Favorites::load() const {
  books_ = BOOK_STATE.getFavoriteBooks();
  loaded_ = true;
}

int Favorites::count() const {
  if (!loaded_) load();
  return static_cast<int>(books_.size());
}

void Favorites::invalidate() const {
  loaded_ = false;
  books_.clear();
}

const std::string& Favorites::pathAt(const int index) const {
  static const std::string empty;
  if (!loaded_) load();
  if (index < 0 || index >= static_cast<int>(books_.size())) return empty;
  return books_[static_cast<size_t>(index)].path;
}

std::string Favorites::thumbnailPath(const std::string& bookPath) const {
  const std::string hash = std::to_string(std::hash<std::string>{}(bookPath));
  const std::string lowerPath = [&bookPath] {
    std::string value = bookPath;
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return value;
  }();

  const char* roots[] = {endsWith(lowerPath, ".xtc") || endsWith(lowerPath, ".xtch") ? "/.metadata/xtc"
                                                                                         : "/.metadata/epub",
                         "/.metadata/pdf", "/.metadata/xtc", "/.system"};
  const char* names[] = {"thumb.jpg", "thumb.png", "thumb.bmp"};
  SdIoMutex::Lock ioLock;
  for (const char* root : roots) {
    const std::string directory = std::string(root) + "/" + (std::string(root) == "/.system" ? "txt_" : "") + hash;
    for (const char* name : names) {
      const std::string path = directory + "/" + name;
      if (SdMan.exists(path.c_str())) return path;
    }
  }
  return {};
}

void Favorites::sourceDimensions(const BookState::Book& book, int& width, int& height) const {
  width = kFallbackSourceWidth;
  height = kFallbackSourceHeight;
  const std::string path = thumbnailPath(book.path);
  if (!path.empty()) {
    int sourceWidth = 0;
    int sourceHeight = 0;
    if (ImageRender::getDimensions(path, &sourceWidth, &sourceHeight) && sourceWidth > 0 && sourceHeight > 0) {
      width = sourceWidth;
      height = sourceHeight;
    }
  }
}

int Favorites::cardWidth(const BookState::Book& book, const int width, const int height) const {
  const int contentHeight = std::max(24, height - kTopPadding - kBottomPadding);
  int sourceWidth = kFallbackSourceWidth;
  int sourceHeight = kFallbackSourceHeight;
  sourceDimensions(book, sourceWidth, sourceHeight);
  const int naturalWidth = std::max(24, static_cast<int>(std::lround(
                                             static_cast<float>(contentHeight) * sourceWidth / sourceHeight)));
  const int maxCardWidth = std::max(24, (width - kHorizontalPadding - kCardGap * 2) * 9 / 20);
  return std::min(naturalWidth, maxCardWidth);
}

Favorites::CardBounds Favorites::cardBounds(const BookState::Book& book, const int cardX, const int y,
                                             const int width, const int height) const {
  const int contentHeight = std::max(24, height - kTopPadding - kBottomPadding);
  const int cardWidthValue = cardWidth(book, width, height);
  int sourceWidth = kFallbackSourceWidth;
  int sourceHeight = kFallbackSourceHeight;
  sourceDimensions(book, sourceWidth, sourceHeight);
  const int cardHeight = std::max(24, std::min(
                                     contentHeight,
                                     static_cast<int>(std::lround(
                                         static_cast<float>(cardWidthValue) * sourceHeight / sourceWidth))));
  const int cardY = y + height - kBottomPadding - cardHeight;
  return {cardX, cardY, cardWidthValue, cardHeight};
}

void Favorites::renderCover(const BookState::Book& book, const CardBounds& bounds,
                            const bool cropToVisibleWidth, const HomeTheme::CarouselShadowStyle shadowStyle,
                            const float cropAnchorX) const {
  const std::string path = thumbnailPath(book.path);
  if (!path.empty()) {
    if (cropToVisibleWidth || SETTINGS.thumbnailSize == SystemSetting::THUMBNAIL_EVEN) {
      ImageRender::Options options;
      setRoundedOptions(options);
      options.cropToFill = true;
      options.cropAnchorX = cropAnchorX;
      renderShadow(renderer_, bounds.x + 6, bounds.y + 6, bounds.width, bounds.height, shadowStyle);
      renderer_.rectangle.fill(bounds.x, bounds.y, bounds.width, bounds.height, false);
      if (ImageRender::create(renderer_, path).render(bounds.x, bounds.y, bounds.width, bounds.height, options)) {
        renderer_.rectangle.render(bounds.x, bounds.y, bounds.width, bounds.height, true,
                                   SETTINGS.bitmapRoundedCorners != 0, SETTINGS.bitmapRoundedCorners == 2);
        return;
      }
    }
    int sourceWidth = 0;
    int sourceHeight = 0;
    if (!ImageRender::getDimensions(path, &sourceWidth, &sourceHeight) || sourceWidth <= 0 || sourceHeight <= 0) {
      sourceWidth = kFallbackSourceWidth;
      sourceHeight = kFallbackSourceHeight;
    }
    const float scale = std::min(static_cast<float>(bounds.width) / sourceWidth,
                                 static_cast<float>(bounds.height) / sourceHeight);
    const int imageWidth = std::max(1, static_cast<int>(std::lround(sourceWidth * scale)));
    const int imageHeight = std::max(1, static_cast<int>(std::lround(sourceHeight * scale)));
    const int imageX = bounds.x + (bounds.width - imageWidth) / 2;
    const int imageY = bounds.y + (bounds.height - imageHeight) / 2;
    ImageRender::Options options;
    setRoundedOptions(options);
    renderShadow(renderer_, imageX + 6, imageY + 6, imageWidth, imageHeight, shadowStyle);
    renderer_.rectangle.fill(imageX, imageY, imageWidth, imageHeight, false);
    if (ImageRender::create(renderer_, path).render(imageX, imageY, imageWidth, imageHeight, options)) {
      renderer_.rectangle.render(imageX, imageY, imageWidth, imageHeight, true,
                                 SETTINGS.bitmapRoundedCorners != 0, SETTINGS.bitmapRoundedCorners == 2);
      return;
    }
  }

  renderShadow(renderer_, bounds.x + 6, bounds.y + 6, bounds.width, bounds.height, shadowStyle);
  renderer_.rectangle.fill(bounds.x, bounds.y, bounds.width, bounds.height, false);
  renderer_.rectangle.render(bounds.x, bounds.y, bounds.width, bounds.height, true,
                             SETTINGS.bitmapRoundedCorners != 0, SETTINGS.bitmapRoundedCorners == 2);
  const int font = MONTSERRAT_10_FONT_ID;
  const std::string title = renderer_.text.truncate(font, bookTitle(book).c_str(), std::max(1, bounds.width - 8));
  const int lineHeight = renderer_.text.getLineHeight(font);
  const int textWidth = renderer_.text.getWidth(font, title.c_str());
  const int textX = bounds.x + std::max(4, (bounds.width - textWidth) / 2);
  const int textY = bounds.y + std::max(4, (bounds.height - lineHeight) / 2);
  renderer_.text.render(font, textX, textY, title.c_str(), true);
}

void Favorites::render(const int index, const int x, const int y, const int width, const int height,
                       const bool background, const HomeTheme::CarouselStyle style, const bool showLabel,
                       const HomeTheme::CarouselLabelColor labelColor,
                       const HomeTheme::CarouselShadowStyle shadowStyle) const {
  if (width <= 0 || height <= 0) return;
  load();
  renderBackground(x, y, width, height, background);
  const ContentArea content = contentArea(y, height, showLabel);
  if (showLabel) renderLabel(x, y, "Favorites", labelColor);
  if (style == HomeTheme::CarouselStyle::Centered) {
    renderCentered(index, x, content.y, width, content.height, shadowStyle);
    return;
  }

  if (books_.empty()) {
    renderer_.text.centered(systemFontId(), content.y + content.height / 2, "No favorites");
    return;
  }

  const int current = ((index % static_cast<int>(books_.size())) + static_cast<int>(books_.size())) %
                      static_cast<int>(books_.size());
  const int visible = std::min(4, static_cast<int>(books_.size()));
  int cardX = x + kHorizontalPadding;
  for (int offset = 0; offset < visible; ++offset) {
    const int bookIndex = (current + offset) % static_cast<int>(books_.size());
    const CardBounds card = cardBounds(books_[static_cast<size_t>(bookIndex)], cardX, content.y, width, content.height);
    if (card.x >= x + width) break;
    CardBounds visibleCard = card;
    visibleCard.width = std::min(card.width, x + width - card.x);
    if (visibleCard.width <= 0) break;
    renderCover(books_[static_cast<size_t>(bookIndex)], visibleCard,
                visibleCard.width < card.width || SETTINGS.thumbnailSize == SystemSetting::THUMBNAIL_EVEN,
                shadowStyle);
    cardX += card.width + kCardGap;
  }
}

void Favorites::preview(const int x, const int y, const int width, const int height, const bool background,
                        const HomeTheme::CarouselStyle style, const bool showLabel,
                        const HomeTheme::CarouselLabelColor labelColor,
                        const HomeTheme::CarouselShadowStyle shadowStyle) const {
  renderBackground(x, y, width, height, background);
  const ContentArea content = contentArea(y, height, showLabel);
  if (showLabel) renderLabel(x, y, "Favorites", labelColor);
  if (style == HomeTheme::CarouselStyle::Centered) {
    previewCentered(x, content.y, width, content.height, shadowStyle);
    return;
  }
  const int cardHeight = std::max(24, content.height - kTopPadding - kBottomPadding);
  const int naturalWidth = std::max(24, static_cast<int>(std::lround(
                                                static_cast<float>(cardHeight) * kFallbackSourceWidth /
                                                kFallbackSourceHeight)));
  const int maxCardWidth = std::max(24, (width - kHorizontalPadding - kCardGap * 2) * 9 / 20);
  const int cardWidth = std::min(naturalWidth, maxCardWidth);
  for (int offset = 0; offset < 3; ++offset) {
    const int cardX = x + kHorizontalPadding + offset * (cardWidth + kCardGap);
    if (cardX >= x + width) break;
    const int visibleWidth = std::min(cardWidth, x + width - cardX);
    const int cardY = content.y + content.height - kBottomPadding - cardHeight;
    renderShadow(renderer_, cardX + 6, cardY + 6, visibleWidth, cardHeight, shadowStyle);
    renderer_.rectangle.fill(cardX, cardY, visibleWidth, cardHeight, false);
    renderer_.rectangle.render(cardX, cardY, visibleWidth, cardHeight, true,
                               SETTINGS.bitmapRoundedCorners != 0, SETTINGS.bitmapRoundedCorners == 2);
  }
}

int Favorites::hitTest(const int index, const int x, const int y, const int areaX, const int areaY, const int areaW,
                       const int areaH, const HomeTheme::CarouselStyle style, const bool showLabel,
                       const HomeTheme::CarouselLabelColor /*labelColor*/) const {
  if (!loaded_) load();
  if (books_.empty() || x < areaX || x >= areaX + areaW || y < areaY || y >= areaY + areaH) return -1;
  const ContentArea content = contentArea(areaY, areaH, showLabel);
  if (style == HomeTheme::CarouselStyle::Centered) {
    return hitTestCentered(index, x, y, areaX, content.y, areaW, content.height);
  }
  const int current = ((index % static_cast<int>(books_.size())) + static_cast<int>(books_.size())) %
                      static_cast<int>(books_.size());
  const int visible = std::min(4, static_cast<int>(books_.size()));
  int cardX = areaX + kHorizontalPadding;
  for (int offset = 0; offset < visible; ++offset) {
    const int bookIndex = (current + offset) % static_cast<int>(books_.size());
    const CardBounds card = cardBounds(books_[static_cast<size_t>(bookIndex)], cardX, content.y, areaW, content.height);
    if (x >= card.x && x < card.x + card.width && y >= card.y && y < card.y + card.height) {
      return (current + offset) % static_cast<int>(books_.size());
    }
    cardX += card.width + kCardGap;
  }
  return -1;
}

void Favorites::renderCentered(const int index, const int x, const int y, const int width, const int height,
                               const HomeTheme::CarouselShadowStyle shadowStyle) const {
  if (books_.empty()) {
    renderer_.text.centered(systemFontId(), y + height / 2, "No favorites");
    return;
  }
  const int current = ((index % static_cast<int>(books_.size())) + static_cast<int>(books_.size())) %
                      static_cast<int>(books_.size());
  const CenteredCarouselLayout layout = centeredBounds(x, y, width, height);
  if (books_.size() > 1) {
    const CardBounds left{layout.sideX, layout.sideY, layout.sideWidth, layout.sideHeight};
    const CardBounds right{layout.centerX + layout.centerWidth + layout.gap, layout.sideY, layout.sideWidth,
                           layout.sideHeight};
    renderCover(books_[static_cast<size_t>((current + books_.size() - 1) % books_.size())], left, true, shadowStyle,
                1.0f);
    renderCover(books_[static_cast<size_t>((current + 1) % books_.size())], right, true, shadowStyle, 0.0f);
  }
  const CardBounds center{layout.centerX, layout.centerY, layout.centerWidth, layout.centerHeight};
  renderCover(books_[static_cast<size_t>(current)], center,
              SETTINGS.thumbnailSize == SystemSetting::THUMBNAIL_EVEN, shadowStyle);
}

void Favorites::previewCentered(const int x, const int y, const int width, const int height,
                                const HomeTheme::CarouselShadowStyle shadowStyle) const {
  const CenteredCarouselLayout layout = centeredBounds(x, y, width, height);
  const CardBounds left{layout.sideX, layout.sideY, layout.sideWidth, layout.sideHeight};
  const CardBounds center{layout.centerX, layout.centerY, layout.centerWidth, layout.centerHeight};
  const CardBounds right{layout.centerX + layout.centerWidth + layout.gap, layout.sideY, layout.sideWidth,
                         layout.sideHeight};
  for (const CardBounds& card : {left, center, right}) {
    renderShadow(renderer_, card.x + 6, card.y + 6, card.width, card.height, shadowStyle);
    renderer_.rectangle.fill(card.x, card.y, card.width, card.height, false);
    renderer_.rectangle.render(card.x, card.y, card.width, card.height, true,
                               SETTINGS.bitmapRoundedCorners != 0, SETTINGS.bitmapRoundedCorners == 2);
  }
}

int Favorites::hitTestCentered(const int index, const int x, const int y, const int areaX, const int areaY,
                               const int areaW, const int areaH) const {
  if (books_.empty()) return -1;
  const int current = ((index % static_cast<int>(books_.size())) + static_cast<int>(books_.size())) %
                      static_cast<int>(books_.size());
  const CenteredCarouselLayout layout = centeredBounds(areaX, areaY, areaW, areaH);
  const CardBounds center{layout.centerX, layout.centerY, layout.centerWidth, layout.centerHeight};
  if (x >= center.x && x < center.x + center.width && y >= center.y && y < center.y + center.height) return current;
  if (books_.size() <= 1) return -1;
  const CardBounds left{layout.sideX, layout.sideY, layout.sideWidth, layout.sideHeight};
  if (x >= left.x && x < left.x + left.width && y >= left.y && y < left.y + left.height) {
    return (current + books_.size() - 1) % books_.size();
  }
  const CardBounds right{layout.centerX + layout.centerWidth + layout.gap, layout.sideY, layout.sideWidth,
                         layout.sideHeight};
  if (x >= right.x && x < right.x + right.width && y >= right.y && y < right.y + right.height) {
    return (current + 1) % books_.size();
  }
  return -1;
}

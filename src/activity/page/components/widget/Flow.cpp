#include "Flow.h"

#include <BitmapRender.h>
#include <Epub/BookMetadataCache.h>
#include <GfxRenderer.h>
#include <ImageRender.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <string>

#include "images/Star.h"
#include "MetadataRating.h"
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

std::string metadataPath(const RecentBook& book) {
  return book.cachePath.empty()
             ? "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(book.path))
             : book.cachePath;
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

void drawCover(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
               const int height, const bool squareCorners = false) {
  renderer.rectangle.fill(x, y, width, height, false);
  const std::string path = thumbnailPath(book.cachePath);
  if (!path.empty()) {
    ImageRender::Options options;
    // Fill the card cover from top to bottom. The 20px inset belongs to the
    // carousel bounds, not inside individual cards.
    options.cropToFill = true;
    options.cropAnchorX = 0.5f;
    options.cropAnchorY = 0.0f;
    options.useDisplayCache = true;
    options.asyncDisplayCache = true;
    if (squareCorners || SETTINGS.bitmapRoundedCorners == 0) {
      options.roundedOutside = BitmapRender::RoundedOutside::None;
    } else if (SETTINGS.bitmapRoundedCorners == 2) {
      options.roundedOutside = BitmapRender::RoundedOutside::SubtleSparseInkAlignedOutside;
    } else {
      options.roundedOutside = BitmapRender::RoundedOutside::SparseInkAlignedOutside;
    }
    if (ImageRender::create(renderer, path).render(x, y, width, height, options)) return;
  }
  renderer.rectangle.render(x, y, width, height, true, squareCorners ? false : SETTINGS.bitmapRoundedCorners != 0,
                           squareCorners ? false : SETTINGS.bitmapRoundedCorners == 2);
}

void drawFlowCard(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                  const int height, const bool center, const std::string& author) {
  renderer.rectangle.fill(x, y, width, height, false);
  renderer.rectangle.render(x, y, width, height, true, SETTINGS.bitmapRoundedCorners != 0,
                           SETTINGS.bitmapRoundedCorners == 2);
  // The reference card is portrait-oriented: the cover spans the full card
  // width, followed by a separate white title/description section.
  const int coverHeight = std::max(20, height * 80 / 100);
  drawCover(renderer, book, x, y, width, coverHeight);
  const int font = center ? MONTSERRAT_16_FONT_ID : MONTSERRAT_10_FONT_ID;
  constexpr int detailMargin = 20;
  const int detailWidth = std::max(1, width - detailMargin * 2);
  const int detailY = y + coverHeight;
  const std::string title = renderer.text.truncate(font, titleFor(book).c_str(), detailWidth,
                                                   EpdFontFamily::BOLD);
  const int titleY = detailY + 10;
  renderer.text.render(font, x + detailMargin, titleY, title.c_str(), true, EpdFontFamily::BOLD);
  if (!author.empty()) {
    const int authorFont = center ? MONTSERRAT_10_FONT_ID : MONTSERRAT_8_FONT_ID;
    const std::string shown = renderer.text.truncate(authorFont, author.c_str(), detailWidth);
    renderer.text.renderGray(authorFont, x + detailMargin, titleY + renderer.text.getLineHeight(font) + 5,
                             shown.c_str(), true, EpdFontFamily::REGULAR);
  }
  renderer.rectangle.render(x, y, width, height, true, SETTINGS.bitmapRoundedCorners != 0,
                            SETTINGS.bitmapRoundedCorners == 2);
}

void drawProgressAndRating(GfxRenderer& renderer, const RecentBook& book, const int x, const int rowY, const int width,
                           const int stars) {
  constexpr int margin = 20;
  constexpr int percentageFont = MONTSERRAT_8_FONT_ID;
  constexpr int barHeight = 5;
  const int percentage = book.progress < 0.0f
                             ? 0
                             : std::max(0, std::min(100, static_cast<int>(book.progress * 100.0f + 0.5f)));
  const std::string percentageText = std::to_string(percentage) + "%";
  const int percentageWidth = renderer.text.getWidth(percentageFont, percentageText.c_str());
  constexpr int starSize = 24;
  constexpr int starGap = 3;
  const int starWidth = stars > 0 ? stars * starSize + (stars - 1) * starGap : 0;
  const int barY = rowY + (renderer.text.getLineHeight(percentageFont) - barHeight) / 2;
  const int left = x + margin;
  const int right = x + width - margin;
  const int progressX = left + percentageWidth + 8;
  const int progressRight = right - (starWidth > 0 ? starWidth + 8 : 0);
  const int barWidth = std::min(std::max(1, width / 2), std::max(1, progressRight - progressX));

  renderer.text.render(percentageFont, left, rowY, percentageText.c_str(), true);
  renderer.rectangle.render(progressX, barY, barWidth, barHeight, true);
  renderer.rectangle.fill(progressX + 1, barY + 1, std::max(1, barWidth - 2), barHeight - 2, false);
  if (percentage > 0) {
    renderer.rectangle.fill(progressX + 1, barY + 1, std::max(1, (barWidth - 2) * percentage / 100),
                            barHeight - 2, true);
  }
  if (stars > 0) {
    const int starsX = right - starWidth;
    for (int star = 0; star < stars; ++star) {
      renderer.bitmap.icon(Star, starsX + star * (starSize + starGap), rowY - (starSize - renderer.text.getLineHeight(percentageFont)) / 2,
                           starSize, starSize);
    }
  }
}

void drawDescription(GfxRenderer& renderer, const std::string& description, const int x, const int y,
                     const int width, const int maxLines) {
  if (description.empty()) return;
  constexpr int font = MONTSERRAT_8_FONT_ID;
  constexpr int margin = 20;
  const int maxWidth = std::max(1, width - margin * 2);
  const int lineHeight = renderer.text.getLineHeight(font);
  size_t position = 0;
  for (int lineIndex = 0; lineIndex < maxLines && position < description.size(); ++lineIndex) {
    while (position < description.size() && std::isspace(static_cast<unsigned char>(description[position]))) ++position;
    if (position >= description.size()) break;
    std::string line;
    size_t next = position;
    while (next < description.size()) {
      while (next < description.size() && std::isspace(static_cast<unsigned char>(description[next]))) ++next;
      const size_t wordStart = next;
      while (next < description.size() && !std::isspace(static_cast<unsigned char>(description[next]))) ++next;
      if (wordStart == next) break;
      const std::string word = description.substr(wordStart, next - wordStart);
      const std::string candidate = line.empty() ? word : line + " " + word;
      if (!line.empty() && renderer.text.getWidth(font, candidate.c_str()) > maxWidth) {
        break;
      }
      line = candidate;
      position = next;
    }
    if (line.empty()) {
      line = renderer.text.truncate(font, description.substr(position).c_str(), maxWidth);
      position = description.size();
    }
    renderer.text.renderGray(font, x + margin, y + lineIndex * lineHeight, line.c_str(), true,
                             EpdFontFamily::REGULAR);
  }
}

void drawPreviewCard(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                     const char* title, const char* author, const bool center) {
  renderer.rectangle.fill(x, y, width, height, false);
  renderer.rectangle.render(x, y, width, height, true, false, false);
  const int coverH = std::max(20, height * 80 / 100);
  const int coverY = y;
  RecentBook placeholder;
  drawCover(renderer, placeholder, x, coverY, width, coverH, true);
  const int titleFont = center ? MONTSERRAT_16_FONT_ID : MONTSERRAT_10_FONT_ID;
  constexpr int detailMargin = 20;
  const int detailY = y + coverH;
  const int detailWidth = std::max(1, width - detailMargin * 2);
  const std::string shownTitle = renderer.text.truncate(titleFont, title, detailWidth, EpdFontFamily::BOLD);
  const int titleY = detailY + 10;
  renderer.text.render(titleFont, x + detailMargin, titleY, shownTitle.c_str(), true, EpdFontFamily::BOLD);
  if (author != nullptr && author[0] != '\0') {
    const int authorFont = center ? MONTSERRAT_10_FONT_ID : MONTSERRAT_8_FONT_ID;
    const std::string shownAuthor = renderer.text.truncate(authorFont, author, detailWidth);
    renderer.text.renderGray(authorFont, x + detailMargin, titleY + renderer.text.getLineHeight(titleFont) + 5,
                             shownAuthor.c_str(), true, EpdFontFamily::REGULAR);
  }
  renderer.rectangle.render(x, y, width, height, true, false, false);
}

void drawFlowArrow(GfxRenderer& renderer, const int x, const int y, const int size, const bool left) {
  const int centerX = x + size / 2;
  const int centerY = y + size / 2;
  const int half = std::max(5, size / 7);
  if (left) {
    renderer.line.render(centerX + half / 2, centerY - half, centerX - half / 2, centerY, true);
    renderer.line.render(centerX - half / 2, centerY, centerX + half / 2, centerY + half, true);
  } else {
    renderer.line.render(centerX - half / 2, centerY - half, centerX + half / 2, centerY, true);
    renderer.line.render(centerX + half / 2, centerY, centerX - half / 2, centerY + half, true);
  }
}

}  // namespace

int Flow::ratingStars(const RecentBook& book) const {
  if (ratingCachePath_ == book.path) return cachedRatingStars_;
  ratingCachePath_ = book.path;
  cachedRatingStars_ = 0;
  BookMetadataCache metadata(metadataPath(book));
  if (metadata.load()) cachedRatingStars_ = metadataRatingStars(metadata.coreMetadata.rating);
  return cachedRatingStars_;
}

const std::string& Flow::descriptionFor(const RecentBook& book) const {
  if (descriptionCachePath_ == book.path) return cachedDescription_;
  descriptionCachePath_ = book.path;
  cachedDescription_.clear();
  BookMetadataCache metadata(metadataPath(book));
  if (metadata.load()) cachedDescription_ = plainDescription(metadata.coreMetadata.description);
  return cachedDescription_;
}

void Flow::render(const int x, const int y, const int width, const int height, const int selectedIndex) const {
  if (width <= 0 || height <= 0) return;
  renderer_.rectangle.fill(x, y, width, height, false);
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty()) {
    renderer_.text.centered(systemFontId(), y + height / 2, "No recent books");
    return;
  }

  constexpr int descriptionLines = 5;
  constexpr int carouselMetadataGap = 40;
  const int metadataFontHeight = renderer_.text.getLineHeight(MONTSERRAT_8_FONT_ID);
  const int descriptionY = y + height - 20 - descriptionLines * metadataFontHeight;
  const int progressRowY = descriptionY - 8 - metadataFontHeight;
  constexpr int carouselInset = 20;
  const int carouselX = x + std::min(carouselInset, width / 4);
  const int carouselWidth = std::max(1, width - (carouselX - x) * 2);
  const int centerWidth = std::min(400, std::max(300, carouselWidth * 40 / 100));
  const int sideWidth = std::max(240, centerWidth * 94 / 100);
  const int cardStep = std::max(140, centerWidth * 52 / 100);
  const int centerX = carouselX + (carouselWidth - centerWidth) / 2;
  const int centerHeight = std::max(150, height * 74 / 100 - 50);
  const int centerY = std::max(y, progressRowY - carouselMetadataGap - centerHeight);
  const int sideHeight = std::max(130, height * 66 / 100 - 50);
  const int sideY = std::max(y, centerY + (centerHeight - sideHeight) / 2 + 8);
  const int bookCount = static_cast<int>(books.size());
  auto bookAt = [&](const int index) -> const RecentBook& {
    const int wrapped = (index % bookCount + bookCount) % bookCount;
    return books[static_cast<size_t>(wrapped)];
  };
  const int centerIndex = ((selectedIndex % bookCount) + bookCount) % bookCount;
  const RecentBook& leftBook = bookAt(centerIndex - 1);
  const RecentBook& centerBook = bookAt(centerIndex);
  const RecentBook& rightBook = bookAt(centerIndex + 1);
  drawFlowCard(renderer_, leftBook, std::max(carouselX, centerX - cardStep), sideY, sideWidth, sideHeight, false,
               leftBook.author.empty() ? "Unknown author" : leftBook.author);
  drawFlowCard(renderer_, rightBook, std::min(carouselX + carouselWidth - sideWidth, centerX + cardStep), sideY,
               sideWidth, sideHeight, false, rightBook.author.empty() ? "Unknown author" : rightBook.author);
  drawFlowCard(renderer_, centerBook, centerX, centerY, centerWidth, centerHeight, true,
               centerBook.author.empty() ? "Unknown author" : centerBook.author);
  drawProgressAndRating(renderer_, centerBook, x, progressRowY, width, ratingStars(centerBook));
  drawDescription(renderer_, descriptionFor(centerBook), x, descriptionY, width, descriptionLines);
  const int arrowSize = std::min(56, std::max(36, height / 6));
  const int arrowY = centerY + (centerHeight - arrowSize) / 2;
  drawFlowArrow(renderer_, carouselX, arrowY, arrowSize, true);
  drawFlowArrow(renderer_, carouselX + carouselWidth - arrowSize, arrowY, arrowSize, false);
}

void Flow::preview(const int x, const int y, const int width, const int height) const {
  if (width <= 0 || height <= 0) return;
  renderer_.rectangle.fill(x, y, width, height, false);
  constexpr int descriptionLines = 5;
  constexpr int carouselMetadataGap = 40;
  const int metadataFontHeight = renderer_.text.getLineHeight(MONTSERRAT_8_FONT_ID);
  const int descriptionY = y + height - 20 - descriptionLines * metadataFontHeight;
  const int progressRowY = descriptionY - 8 - metadataFontHeight;
  constexpr int carouselInset = 20;
  const int carouselX = x + std::min(carouselInset, width / 4);
  const int carouselWidth = std::max(1, width - (carouselX - x) * 2);
  const int centerWidth = std::min(400, std::max(300, carouselWidth * 40 / 100));
  const int sideWidth = std::max(240, centerWidth * 94 / 100);
  const int cardStep = std::max(140, centerWidth * 52 / 100);
  const int centerX = carouselX + (carouselWidth - centerWidth) / 2;
  const int centerHeight = std::max(150, height * 74 / 100 - 50);
  const int centerY = std::max(y, progressRowY - carouselMetadataGap - centerHeight);
  const int sideHeight = std::max(130, height * 66 / 100 - 50);
  const int sideY = std::max(y, centerY + (centerHeight - sideHeight) / 2 + 8);
  drawPreviewCard(renderer_, std::max(carouselX, centerX - cardStep), sideY, sideWidth, sideHeight, "White Daisy",
                  "M. Rivera", false);
  drawPreviewCard(renderer_, std::min(carouselX + carouselWidth - sideWidth, centerX + cardStep), sideY, sideWidth, sideHeight,
                  "Pink Rose", "L. Santos", false);
  drawPreviewCard(renderer_, centerX, centerY, centerWidth, centerHeight, "Sunflower",
                  "A. Gardner", true);
  RecentBook previewBook;
  previewBook.progress = 0.65f;
  drawProgressAndRating(renderer_, previewBook, x, progressRowY, width, 5);
  drawDescription(renderer_, "A bright, cheerful flower known for its large yellow petals.", x, descriptionY, width,
                  descriptionLines);
  const int arrowSize = std::min(56, std::max(36, height / 6));
  const int arrowY = centerY + (centerHeight - arrowSize) / 2;
  drawFlowArrow(renderer_, carouselX, arrowY, arrowSize, true);
  drawFlowArrow(renderer_, carouselX + carouselWidth - arrowSize, arrowY, arrowSize, false);
}

bool Flow::hitTest(const int x, const int y, const int boundsX, const int boundsY, const int boundsWidth,
                   const int boundsHeight) const {
  if (boundsWidth <= 0 || boundsHeight <= 0) return false;
  constexpr int descriptionLines = 5;
  constexpr int carouselMetadataGap = 40;
  const int metadataFontHeight = renderer_.text.getLineHeight(MONTSERRAT_8_FONT_ID);
  const int descriptionY = boundsY + boundsHeight - 20 - descriptionLines * metadataFontHeight;
  const int progressRowY = descriptionY - 8 - metadataFontHeight;
  constexpr int carouselInset = 20;
  const int carouselX = boundsX + std::min(carouselInset, boundsWidth / 4);
  const int carouselWidth = std::max(1, boundsWidth - (carouselX - boundsX) * 2);
  const int centerWidth = std::min(400, std::max(300, carouselWidth * 40 / 100));
  const int centerX = carouselX + (carouselWidth - centerWidth) / 2;
  const int centerHeight = std::max(150, boundsHeight * 74 / 100 - 50);
  const int centerY = std::max(boundsY, progressRowY - carouselMetadataGap - centerHeight);
  return x >= centerX && x < centerX + centerWidth && y >= centerY && y < centerY + centerHeight;
}

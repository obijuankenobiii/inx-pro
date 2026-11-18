#include "Thumb.h"

#include <Arduino.h>
#include <BitmapRender.h>
#include <GfxRenderer.h>
#include <ImageRender.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>
#include <string>
#include <utility>

#include "state/SystemSetting.h"
#include "state/BookProgress.h"
#include "state/RecentBooks.h"
#include "images/Star.h"
#include "system/UiLayout.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/SdIoMutex.h"
#include "../../navigation/Menu.h"

namespace views {
namespace library {

namespace {

int contentBottomInset() {
  // The hidden sidebar replaces the bottom tab bar, but thumbnails still need
  // a touch-safe breathing room at the bottom of the portrait panel.
  return SETTINGS.hiddenMenu != 0 ? UiLayout::LIBRARY_BOTTOM_BAR_HEIGHT : navigation::Menu::bottomInset();
}

std::string bookTitle(const LibraryIndex::Book& book) {
  std::string title = book.title;
  if (title.empty()) {
    const size_t slash = book.path.find_last_of('/');
    const size_t start = slash == std::string::npos ? 0 : slash + 1;
    const size_t dot = book.path.find_last_of('.');
    const size_t end = dot == std::string::npos || dot < start ? book.path.size() : dot;
    title = book.path.substr(start, end - start);
  }
  return book.author.empty() ? title : book.author + " - " + title;
}

std::string folderTitle(const LibraryIndex::Book& folder) {
  // Author/series/tag groups use a synthetic hashed path for navigation, but
  // their user-facing label is stored separately in the indexed title.
  if (folder.hasMetadata && !folder.title.empty()) return folder.title;
  if (folder.path == "/" || folder.path.empty()) return "Library";
  const size_t end = folder.path.find_last_not_of('/');
  if (end == std::string::npos) return "Library";
  const size_t slash = folder.path.find_last_of('/', end);
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  return folder.path.substr(start, end - start + 1);
}

void drawTitle(const GfxRenderer& renderer, const std::string& value, const int x, const int y, const int width,
               const int font, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const std::string title = renderer.text.truncate(font, value.c_str(), std::max(8, width), style);
  renderer.text.render(font, x, y, title.c_str(), true, style);
}

void drawFavoriteBadge(const GfxRenderer& renderer, const int x, const int y, const int width, const bool favorite,
                       const bool topRight = true) {
  if (!favorite) return;
  constexpr int iconSize = 24;
  constexpr int badgePadding = 5;
  constexpr int badgeMargin = 5;
  constexpr int badgeSize = iconSize + badgePadding * 2;
  const int badgeX = topRight ? x + std::max(0, width - badgeSize - badgeMargin) : x + badgeMargin;
  const int badgeY = y + badgeMargin;
  renderer.rectangle.fill(badgeX, badgeY, badgeSize, badgeSize, static_cast<int>(GfxRenderer::FillTone::Ink), true);
  renderer.bitmap.icon(Star, badgeX + badgePadding, badgeY + badgePadding, iconSize, iconSize,
                       BitmapRender::Orientation::None, true);
}

void drawFolderBookCountBadge(const GfxRenderer& renderer, const int x, const int y, const int width,
                              const int height, const int bookCount) {
  if (bookCount <= 0 || width < 8 || height < 8) return;
  constexpr int paddingX = 6;
  constexpr int paddingY = 4;
  constexpr int margin = 5;
  const int font = systemFontId();
  const std::string label = std::string("+") + std::to_string(bookCount);
  const int badgeWidth = renderer.text.getWidth(font, label.c_str()) + paddingX * 2;
  const int badgeHeight = renderer.text.getLineHeight(font) + paddingY * 2;
  const int badgeX = x + std::max(0, width - badgeWidth - margin);
  const int badgeY = y + margin;
  renderer.rectangle.fill(badgeX, badgeY, badgeWidth, badgeHeight,
                          static_cast<int>(GfxRenderer::FillTone::Ink), true);
  renderer.rectangle.render(badgeX, badgeY, badgeWidth, badgeHeight, false, true);
  renderer.text.render(font, badgeX + paddingX, badgeY + paddingY, label.c_str(), false);
}

void drawItemBadge(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                   const std::string& label) {
  if (label.empty() || width < 8 || height < 8) return;
  constexpr int paddingX = 6;
  constexpr int paddingY = 4;
  constexpr int margin = 5;
  const int font = systemFontIdForPointSize(8);
  const int maxTextWidth = std::max(1, width - paddingX * 2);
  const std::string shown = renderer.text.truncate(font, label.c_str(), maxTextWidth);
  const int badgeWidth = std::min(width, renderer.text.getWidth(font, shown.c_str()) + paddingX * 2);
  const int badgeHeight = std::min(height, renderer.text.getLineHeight(font) + paddingY * 2);
  const int badgeX = x + std::max(0, width - badgeWidth - std::min(margin, width - badgeWidth));
  const int badgeY = y + std::min(margin, height - badgeHeight);
  renderer.rectangle.fill(badgeX, badgeY, badgeWidth, badgeHeight,
                          static_cast<int>(GfxRenderer::FillTone::Ink), true);
  renderer.rectangle.render(badgeX, badgeY, badgeWidth, badgeHeight, false, true);
  renderer.text.render(font, badgeX + std::min(paddingX, badgeWidth / 2),
                       badgeY + std::max(0, (badgeHeight - renderer.text.getLineHeight(font)) / 2), shown.c_str(), false);
}

std::string parent(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return "/";
  return path.substr(0, slash);
}

std::string imagePathForBook(const std::string& bookPath) {
  std::string extension = bookPath;
  std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  const bool xtc = extension.size() >= 4 && extension.substr(extension.size() - 4) == ".xtc";
  const char* roots[] = {xtc ? "/.metadata/xtc" : "/.metadata/epub",
                         xtc ? "/.metadata/epub" : "/.metadata/xtc"};
  const char* names[] = {"thumb.jpg", "thumb.png", "thumb.bmp", "cover.jpg", "cover.png", "cover.bmp"};
  for (const char* root : roots) {
    const std::string directory = std::string(root) + "/" + std::to_string(std::hash<std::string>{}(bookPath));
    for (const char* name : names) {
      const std::string path = directory + "/" + name;
      if (SdMan.exists(path.c_str())) return path;
    }
  }
  return {};
}

std::string folderImagePath(const std::string& folder) {
  const char* names[] = {"thumb.jpg", "thumb.png", "thumb.bmp", "cover.jpg", "cover.png", "cover.bmp"};
  const std::string base = folder == "/" ? std::string() : folder;
  for (const char* name : names) {
    const std::string path = base + "/" + name;
    if (SdMan.exists(path.c_str())) return path;
  }
  return imagePathForBook(folder);
}

void drawContrastFrame(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                       const bool rounded) {
  if (width < 1 || height < 1) return;
  renderer.rectangle.render(x, y, width, height, true, rounded);
  // A white inset frame produces sharp white pixels at the lower corners of
  // rounded covers, especially when the artwork has a dark/gray shadow.
  // The outer border is sufficient for rounded thumbnails; retain the inset
  // cleanup only for square thumbnails.
  if (!rounded && width > 3 && height > 3) {
    renderer.rectangle.render(x + 1, y + 1, width - 2, height - 2, false, rounded);
  }
}

int folderBookCount(const std::string& folder, const std::vector<LibraryIndex::Book>& books) {
  const std::string prefix = folder == "/" ? "/" : folder + "/";
  return static_cast<int>(std::count_if(books.begin(), books.end(), [&prefix](const LibraryIndex::Book& book) {
    return book.type == LibraryIndex::Book::Type::BOOK && book.path.compare(0, prefix.size(), prefix) == 0;
  }));
}

int folderBookCount(const LibraryIndex::Book& folder, const std::vector<LibraryIndex::Book>& books) {
  return folder.hasMetadata ? folder.bookCount : folderBookCount(folder.path, books);
}

const LibraryIndex::Book* singleBookInFolder(const std::string& folder,
                                             const std::vector<LibraryIndex::Book>& books) {
  const std::string prefix = folder == "/" ? "/" : folder + "/";
  const LibraryIndex::Book* result = nullptr;
  for (const LibraryIndex::Book& book : books) {
    if (book.type != LibraryIndex::Book::Type::BOOK || book.path.compare(0, prefix.size(), prefix) != 0) continue;
    if (result != nullptr) return nullptr;
    result = &book;
  }
  return result;
}

int folderChildCount(const std::string& folder, const std::vector<LibraryIndex::Book>& books) {
  return static_cast<int>(std::count_if(books.begin(), books.end(), [&folder](const LibraryIndex::Book& book) {
    return book.type == LibraryIndex::Book::Type::FOLDER && parent(book.path) == folder;
  }));
}

bool cover(GfxRenderer& renderer, const std::string& path, const int x, const int y, const int width,
           const int height, const bool rounded, const bool frame = true, const bool cropToFill = false) {
  if (path.empty() || width < 8 || height < 8) return false;
  renderer.rectangle.fill(x, y, width, height, false, rounded);
  ImageRender::Options options;
  options.cropToFill = cropToFill;
  options.cropAnchorX = 0.5f;
  options.useDisplayCache = true;
  options.asyncDisplayCache = true;
  options.roundedOutside = rounded ? BitmapRender::RoundedOutside::PaperOutside
                                   : BitmapRender::RoundedOutside::None;
  if (!ImageRender::create(renderer, path).render(x, y, width, height, options)) {
    return false;
  }
  if (frame) drawContrastFrame(renderer, x, y, width, height, rounded);
  return true;
}

// Keep a small gap between the cover and the title at the bottom of a tile.
constexpr int kFolderLabelGap = 10;

void folderLabel(GfxRenderer& renderer, const std::string& value, const int labelX, const int labelWidthAvailable,
                 const int y, const int height, const int bottomOffset = 5) {
  const int font = systemFontId();
  const int lineHeight = renderer.text.getLineHeight(font);
  constexpr int horizontalMargin = 10;
  const int textX = labelX + horizontalMargin;
  const int textWidth = std::max(8, labelWidthAvailable - horizontalMargin * 2);
  const std::string label = renderer.text.truncate(font, value.c_str(), textWidth);
  renderer.text.render(font, textX, y + height - lineHeight - bottomOffset, label.c_str(), true);
}

void folderLabelWithCount(GfxRenderer& renderer, const std::string& value, const int labelX,
                          const int labelWidthAvailable, const int y, const int height, const int itemCount) {
  const int font = systemFontId();
  const int lineHeight = renderer.text.getLineHeight(font);
  constexpr int horizontalMargin = 10;
  constexpr int lineGap = 2;
  const int textX = labelX + horizontalMargin;
  const int textWidth = std::max(8, labelWidthAvailable - horizontalMargin * 2);
  const std::string title = renderer.text.truncate(font, value.c_str(), textWidth);
  const std::string count = itemCount == 1 ? "1 item" : std::to_string(itemCount) + " items";
  const int countY = y + height - lineHeight - 5;
  const int titleY = countY - lineHeight - lineGap;
  renderer.text.render(font, textX, titleY, title.c_str(), true);
  if (itemCount > 0) renderer.text.renderGray(font, textX, countY, count.c_str(), true, EpdFontFamily::REGULAR);
}

void bookLabelAfterThumbnail(GfxRenderer& renderer, const std::string& value, const int labelX,
                             const int labelWidthAvailable, const int imageY, const int imageHeight) {
  const int font = systemFontId();
  constexpr int horizontalMargin = 10;
  const int textX = labelX + horizontalMargin;
  const int textWidth = std::max(8, labelWidthAvailable - horizontalMargin * 2);
  const std::string label = renderer.text.truncate(font, value.c_str(), textWidth);
  renderer.text.render(font, textX, imageY + imageHeight + 2, label.c_str(), true);
}

void bookLabelWithProgress(GfxRenderer& renderer, const std::string& value, const int labelX,
                           const int labelWidthAvailable, const int imageY, const int imageHeight,
                           const int progressPercent) {
  const int font = systemFontId();
  const int lineHeight = renderer.text.getLineHeight(font);
  constexpr int horizontalMargin = 10;
  constexpr int lineGap = 2;
  const int textX = labelX + horizontalMargin;
  const int textWidth = std::max(8, labelWidthAvailable - horizontalMargin * 2);
  const std::string title = renderer.text.truncate(font, value.c_str(), textWidth);
  const int clampedProgress = std::max(0, std::min(100, progressPercent));
  const std::string progress = std::to_string(clampedProgress) + "%";
  const int titleY = imageY + imageHeight + 2;
  renderer.text.render(font, textX, titleY, title.c_str(), true);
  renderer.text.renderGray(font, textX, titleY + lineHeight + lineGap, progress.c_str(), true,
                           EpdFontFamily::REGULAR);
}

int progressPercentForBook(const std::string& path) {
  for (const RecentBook& recent : RECENT_BOOKS.getBooks()) {
    if (recent.path == path && recent.progress >= 0.0f) {
      return std::max(0, std::min(100, static_cast<int>(std::lround(recent.progress * 100.0f))));
    }
  }

  BookProgress progress("/.metadata/epub/" + std::to_string(std::hash<std::string>{}(path)));
  BookProgress::Data data;
  if (progress.load(data)) {
    return std::max(0, std::min(100, static_cast<int>(std::lround(data.progressPercent))));
  }
  return 0;
}

void placeholder(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                 const std::string& value) {
  const int font = systemFontId();
  renderer.rectangle.fill(x, y, width, height, false);
  renderer.rectangle.render(x, y, width, height, true);
  const int lineHeight = renderer.text.getLineHeight(font);
  const int maxLines = std::max(1, (height - 12) / std::max(1, lineHeight));
  const int maxWidth = std::max(8, width - 12);
  int lineCount = 0;
  bool inWord = false;
  for (const char c : value) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      inWord = false;
    } else if (!inWord) {
      inWord = true;
      ++lineCount;
    }
  }
  lineCount = std::min(maxLines, lineCount);
  int lineY = y + std::max(4, (height - lineCount * lineHeight) / 2);
  size_t position = 0;
  for (int line = 0; line < lineCount; ++line) {
    while (position < value.size() && std::isspace(static_cast<unsigned char>(value[position]))) ++position;
    size_t end = position;
    while (end < value.size() && !std::isspace(static_cast<unsigned char>(value[end]))) ++end;
    const std::string word = renderer.text.truncate(font, value.substr(position, end - position).c_str(), maxWidth);
    const int textWidth = renderer.text.getWidth(font, word.c_str());
    renderer.text.render(font, x + (width - textWidth) / 2, lineY, word.c_str(), true);
    lineY += lineHeight;
    position = end;
  }
}

void drawFolderPlaceholder(GfxRenderer& renderer, const LibraryIndex::Book& item, const int x, const int y,
                           const int width, const int height, const bool favorite, const int bookCount,
                           const int folderCount = 0, const std::string& titleOverride = {},
                           const bool inlineCount = false) {
  const bool hideTitle = SETTINGS.hideThumbnailTitles != 0;
  constexpr int padding = 3;
  const int lineHeight = hideTitle ? 0 : renderer.text.getLineHeight(systemFontId());
  const int titleLines = inlineCount ? 2 : 1;
  const int titleHeight = hideTitle ? 0 : lineHeight * titleLines + kFolderLabelGap;
  const int imageY = y + padding;
  const int imageHeight = std::max(8, height - titleHeight - padding * 2);
  const int emptyWidth = std::max(8, width - padding * 2);
  const int emptyX = x + (width - emptyWidth) / 2;
  const std::string title = titleOverride.empty() ? folderTitle(item) : titleOverride;
  placeholder(renderer, emptyX, imageY, emptyWidth, imageHeight, hideTitle ? title : "");
  if (!hideTitle) {
    if (inlineCount) {
      folderLabelWithCount(renderer, title, emptyX, emptyWidth, y, height, bookCount + folderCount);
    } else {
      folderLabel(renderer, title, emptyX, emptyWidth, y, height);
    }
  }
  drawFavoriteBadge(renderer, emptyX, imageY, emptyWidth, favorite, false);
  if (!inlineCount) drawFolderBookCountBadge(renderer, emptyX, imageY, emptyWidth, imageHeight, bookCount);
}

}

Thumb::Thumb(GfxRenderer& renderer, MappedInputManager& mappedInput,
             const std::vector<LibraryIndex::Book>& items, const std::vector<LibraryIndex::Book>& books,
             std::function<void(int, bool)> select,
             std::function<bool(const LibraryIndex::Book&)> isFavorite,
             std::function<void(int, int)> outsideTap,
             std::function<std::vector<std::string>(const LibraryIndex::Book&, int)> folderCovers)
    : renderer(renderer), mappedInput(mappedInput), items(items), books(books), select(std::move(select)),
      isFavorite(std::move(isFavorite)),
      outsideTap(std::move(outsideTap)), customFolderCovers(std::move(folderCovers)) {}

std::string Thumb::thumbnailPathForBook(const std::string& bookPath) {
  const auto cached = thumbnailPathCache_.find(bookPath);
  if (cached != thumbnailPathCache_.end() && !cached->second.empty() &&
      SdMan.exists(cached->second.c_str())) {
    return cached->second;
  }

  // An empty result is not permanent. Thumbnail generation can finish after
  // the library page has been built, so probe again instead of retaining the
  // old miss forever.
  const std::string path = imagePathForBook(bookPath);
  thumbnailPathCache_[bookPath] = path;
  return path;
}

std::vector<std::string> Thumb::coversForFolder(const LibraryIndex::Book& folder, const int limit) {
  if (customFolderCovers) {
    const std::vector<std::string> custom = customFolderCovers(folder, limit);
    if (!custom.empty()) return custom;
  }
  std::vector<std::string> covers;
  if (limit <= 0) return covers;

  auto collect = [&](const bool directOnly, const int maxChecked) {
    int checked = 0;
    const std::string prefix = folder.path == "/" ? "/" : folder.path + "/";
    for (const LibraryIndex::Book& book : books) {
      if (book.type != LibraryIndex::Book::Type::BOOK) continue;
      if (directOnly ? parent(book.path) != folder.path
                     : book.path.compare(0, prefix.size(), prefix) != 0) {
        continue;
      }
      if (checked++ >= maxChecked) break;
      const std::string path = thumbnailPathForBook(book.path);
      if (path.empty()) continue;
      covers.push_back(path);
      if (static_cast<int>(covers.size()) >= limit) break;
    }
  };

  collect(true, 32);
  if (covers.empty()) collect(false, 64);
  return covers;
}

void Thumb::getThumbnailSize(GfxRenderer& renderer, int& width, int& height) {
  constexpr int gridMargin = 0;
  const int availableWidth = renderer.getScreenWidth() - sideMargin * 2;
  const int availableHeight = renderer.getScreenHeight() - navigation::Menu::height -
                              contentBottomInset() - gridMargin * 2;
  const ThumbLayout standard = standardThumbLayout();
  const int cellWidth = (availableWidth - standard.columnGap * (standard.columns - 1)) / standard.columns;
  const int cellHeight = (availableHeight - standard.rowGap * (standard.rows - 1)) / standard.rows;
  width = std::max(40, cellWidth);
  height = std::max(40, cellHeight);
}

void Thumb::setRoot(const bool value) { root = value; }

int Thumb::itemsPerPage() const { return columnCount() * rowCount(); }

void Thumb::load() {
  if (page > 0 && page >= pageCount()) page = std::max(0, pageCount() - 1);
  calculate();
  loadAt = millis() + 20;
  thumbnails.clear();
  const int start = page * itemsPerPage();
  const int count = std::min(itemsPerPage(), static_cast<int>(items.size()) - start);
  thumbnails.reserve(std::max(0, count));
  for (int index = 0; index < count; ++index) {
    const LibraryIndex::Book& item = items[static_cast<size_t>(start + index)];
    Thumbnail thumbnail;
    thumbnail.item = item.path;
    std::string folderImage;
    if (item.type == LibraryIndex::Book::Type::FOLDER) {
      SdIoMutex::Lock lock;
      folderImage = folderImagePath(item.path);
      if (folderImage.empty()) {
        const std::vector<std::string> covers = coversForFolder(item, 1);
        if (!covers.empty()) thumbnail.first = covers[0];
      }
    }
    if (item.type == LibraryIndex::Book::Type::FOLDER) {
      thumbnail.bookCount = folderBookCount(item, books);
      thumbnail.folderCount = item.hasMetadata ? item.folderCount : folderChildCount(item.path, books);
      if (thumbnail.bookCount == 1) {
        if (const LibraryIndex::Book* book = singleBookInFolder(item.path, books)) {
          thumbnail.singleBookTitle = bookTitle(*book);
        }
      }
    } else {
      // Resolve an existing cover during the initial page setup. Waiting for
      // loadNext() made an already-generated cache appear as a white tile for
      // at least one frame, and an input event could postpone that pass.
      thumbnail.image = thumbnailPathForBook(item.path);
      thumbnail.loaded = !thumbnail.image.empty();
      if (layout.titleLines > 1) thumbnail.progressPercent = progressPercentForBook(item.path);
    }
    const auto cached = resolvedCache_.find(item.path);
    const bool cachedImageStillExists =
        cached != resolvedCache_.end() &&
        ((!cached->second.image.empty() && SdMan.exists(cached->second.image.c_str())) ||
         (!cached->second.first.empty() && SdMan.exists(cached->second.first.c_str())));
    if (cachedImageStillExists) {
      const int bookCount = thumbnail.bookCount;
      const int folderCount = thumbnail.folderCount;
      const int progressPercent = thumbnail.progressPercent;
      const std::string singleBookTitle = thumbnail.singleBookTitle;
      thumbnail = cached->second;
      thumbnail.item = item.path;
      thumbnail.bookCount = bookCount;
      thumbnail.folderCount = folderCount;
      if (!singleBookTitle.empty()) thumbnail.singleBookTitle = singleBookTitle;
      thumbnail.progressPercent = progressPercent;
      thumbnail.loaded = true;
    }
    if (!folderImage.empty()) {
      thumbnail.first.clear();
      thumbnail.image = folderImage;
      thumbnail.loaded = true;
      resolvedCache_[thumbnail.item] = thumbnail;
    } else if (!thumbnail.first.empty()) {
      // A folder without its own cover uses the first book cover. Resolve it
      // during the initial page setup as well, so a durable display cache can
      // be drawn on the first frame instead of after loadNext().
      thumbnail.loaded = true;
      resolvedCache_[thumbnail.item] = thumbnail;
    }
    thumbnails.push_back(std::move(thumbnail));
  }
}

bool Thumb::loadNext() {
  const unsigned long now = millis();
  if (mappedInput.isTouchPressed()) {
    loadAt = now + 20;
    return false;
  }
  if (static_cast<long>(now - loadAt) < 0) return false;

  const int limit = itemsPerPage();
  int loaded = 0;
  std::array<Thumbnail*, maxItemsPerPage> booksToMeasure{};
  int bookCount = 0;
  std::array<Thumbnail*, maxItemsPerPage> resolvedThisCall{};
  int resolvedCount = 0;
  {
    SdIoMutex::Lock lock;
    for (Thumbnail& thumbnail : thumbnails) {
      if (thumbnail.loaded) continue;
      const auto item = std::find_if(items.begin(), items.end(), [&thumbnail](const LibraryIndex::Book& book) {
        return book.path == thumbnail.item;
      });
      if (item != items.end()) {
        if (item->type == LibraryIndex::Book::Type::FOLDER) {
          const std::string folderImage = folderImagePath(item->path);
          if (!folderImage.empty()) {
            thumbnail.first.clear();
            thumbnail.image = folderImage;
          } else {
            const std::vector<std::string> covers = coversForFolder(*item, 1);
            if (!covers.empty()) thumbnail.first = covers[0];
          }
          if (thumbnail.bookCount == 1) {
            if (const LibraryIndex::Book* book = singleBookInFolder(item->path, books)) {
              thumbnail.singleBookTitle = bookTitle(*book);
            }
          }
        } else {
          thumbnail.image = thumbnailPathForBook(item->path);
          if (!thumbnail.image.empty() && bookCount < static_cast<int>(booksToMeasure.size())) {
            booksToMeasure[bookCount++] = &thumbnail;
          }
          if (layout.titleLines > 1) thumbnail.progressPercent = progressPercentForBook(item->path);
        }
      }
      thumbnail.loaded = true;
      if (resolvedCount < static_cast<int>(resolvedThisCall.size())) {
        resolvedThisCall[resolvedCount++] = &thumbnail;
      }
      if (++loaded == limit) break;
    }
  }
  for (int index = 0; index < bookCount; ++index) {
    Thumbnail* thumbnail = booksToMeasure[static_cast<size_t>(index)];
    ImageRender::getDimensions(thumbnail->image, &thumbnail->imageWidth, &thumbnail->imageHeight);
  }
  for (int index = 0; index < resolvedCount; ++index) {
    const Thumbnail* thumbnail = resolvedThisCall[static_cast<size_t>(index)];
    resolvedCache_[thumbnail->item] = *thumbnail;
  }
  return loaded > 0;
}

bool Thumb::prefetchNextPage() {
  if (mappedInput.isTouchPressed()) return false;
  const int perPage = itemsPerPage();
  const int start = (page + 1) * perPage;
  if (start >= static_cast<int>(items.size())) return false;
  const int end = std::min(start + perPage, static_cast<int>(items.size()));

  for (int index = start; index < end; ++index) {
    const LibraryIndex::Book& item = items[static_cast<size_t>(index)];
    if (resolvedCache_.find(item.path) != resolvedCache_.end()) continue;

    Thumbnail thumbnail;
    thumbnail.item = item.path;
    {
      SdIoMutex::Lock lock;
      if (item.type == LibraryIndex::Book::Type::FOLDER) {
        const std::string folderImage = folderImagePath(item.path);
        if (!folderImage.empty()) {
          thumbnail.image = folderImage;
        } else {
          const std::vector<std::string> covers = coversForFolder(item, 1);
          if (!covers.empty()) thumbnail.first = covers[0];
        }
        thumbnail.bookCount = folderBookCount(item, books);
        thumbnail.folderCount = item.hasMetadata ? item.folderCount : folderChildCount(item.path, books);
        if (thumbnail.bookCount == 1) {
          if (const LibraryIndex::Book* book = singleBookInFolder(item.path, books)) {
            thumbnail.singleBookTitle = bookTitle(*book);
          }
        }
      } else {
        thumbnail.image = thumbnailPathForBook(item.path);
        if (layout.titleLines > 1) thumbnail.progressPercent = progressPercentForBook(item.path);
      }
    }
    if (item.type != LibraryIndex::Book::Type::FOLDER && !thumbnail.image.empty()) {
      ImageRender::getDimensions(thumbnail.image, &thumbnail.imageWidth, &thumbnail.imageHeight);
    }
    thumbnail.loaded = true;
    resolvedCache_[thumbnail.item] = thumbnail;
    int cellX = 0;
    int cellY = 0;
    int cellWidth = 0;
    int cellHeight = 0;
    itemBounds(index - start, cellX, cellY, cellWidth, cellHeight);
    if (item.type == LibraryIndex::Book::Type::FOLDER) {
      preloadFolderCache(thumbnail, cellX, cellY, cellWidth, cellHeight);
    } else {
      preloadBookCache(thumbnail, cellX, cellY, cellWidth, cellHeight);
    }
    return true;
  }
  return false;
}

void Thumb::reset() {
  page = 0;
  thumbnailPathCache_.clear();
  // A previous page can have resolved a missing cache as an empty thumbnail.
  // Drop those resolutions so newly generated covers are discovered on the
  // next load. Valid image files are cheap to resolve again from the index.
  resolvedCache_.clear();
}

int Thumb::pageCount() const {
  const int perPage = itemsPerPage();
  return std::max(1, (static_cast<int>(items.size()) + perPage - 1) / perPage);
}

int Thumb::top() const { return navigation::Menu::height; }

void Thumb::calculate() {
  // Keep the label gap and cover area inside the tile while using the outer
  // margin to give each thumbnail a little more height.
  constexpr int gridMargin = 0;
  const int availableWidth = renderer.getScreenWidth() - sideMargin * 2;
  const int availableHeight = renderer.getScreenHeight() - top() - contentBottomInset() - gridMargin * 2 - layout.topGap;
  const int columns = columnCount();
  const int rows = rowCount();
  const int width = std::max(40, (availableWidth - gap * (columns - 1)) / columns + layout.cellWidthExtra);
  const int fullHeight = std::max(40, (availableHeight - layout.rowGap * (rows - 1)) / rows);
  const int height = layout.cellHeight == nullptr ? fullHeight : layout.cellHeight(width, fullHeight);
  const int gridWidth = width * columns + layout.columnGap * (columns - 1);
  const int gridHeight = height * rows + layout.rowGap * (rows - 1);
  const int x = sideMargin + (layout.alignTop ? 0 : std::max(0, (availableWidth - gridWidth) / 2));
  const int y = top() + gridMargin + layout.topGap +
                (layout.alignTop ? 0 : std::max(0, (availableHeight - gridHeight) / 2));

  for (int index = 0; index < itemsPerPage(); ++index) {
    Rect& cell = cells[static_cast<size_t>(index)];
    cell.width = width;
    cell.height = height;
    cell.x = x + (index % columns) * (width + layout.columnGap);
    cell.y = y + (index / columns) * (height + layout.rowGap);
  }
}

void Thumb::preloadImageCache(const std::string& path, const int x, const int y, const int width, const int height,
                              const bool cropToFill, const float cropAnchorX) const {
  if (path.empty() || width < 8 || height < 8) return;
  ImageRender::Options options;
  options.cropToFill = cropToFill;
  options.cropAnchorX = cropAnchorX;
  options.useDisplayCache = true;
  options.roundedOutside = SETTINGS.bitmapRoundedCorners != 0
                               ? BitmapRender::RoundedOutside::PaperOutside
                               : BitmapRender::RoundedOutside::None;
  ImageRender::create(renderer, path).preloadDisplayCache(x, y, width, height, options);
}

void Thumb::preloadBookCache(const Thumbnail& thumbnail, const int x, const int y, const int width,
                             const int height) const {
  if (thumbnail.image.empty()) return;
  constexpr int padding = 3;
  const bool hideTitle = SETTINGS.hideThumbnailTitles != 0;
  const int lineHeight = hideTitle ? 0 : renderer.text.getLineHeight(systemFontId());
  const int titleHeight = hideTitle ? 0 : lineHeight * layout.titleLines + kFolderLabelGap;
  const int imageAreaY = y + padding;
  const int imageAreaWidth = std::max(8, width - padding * 2);
  const int imageAreaHeight = std::max(8, height - titleHeight - padding * 2);
  preloadImageCache(thumbnail.image, x + padding, imageAreaY, imageAreaWidth, imageAreaHeight, true, 0.5f);
}

void Thumb::preloadFolderCache(const Thumbnail& thumbnail, const int x, const int y, const int width,
                               const int height) const {
  const std::string& frontPath = thumbnail.first.empty() ? thumbnail.image : thumbnail.first;
  if (frontPath.empty()) return;

  const bool hideTitle = SETTINGS.hideThumbnailTitles != 0;
  constexpr int padding = 3;
  const int lineHeight = hideTitle ? 0 : renderer.text.getLineHeight(systemFontId());
  const int titleHeight = hideTitle ? 0 : lineHeight * layout.titleLines + kFolderLabelGap;
  const int imageY = y + padding;
  const int imageHeight = std::max(8, height - titleHeight - padding * 2);
  const int imageWidth = std::max(12, width - padding * 2);
  preloadImageCache(frontPath, x + padding, imageY, imageWidth, imageHeight, true, 0.5f);
}

const Thumb::Thumbnail* Thumb::find(const std::string& item) const {
  const auto match = std::find_if(thumbnails.begin(), thumbnails.end(), [&item](const Thumbnail& thumbnail) {
    return thumbnail.item == item;
  });
  return match == thumbnails.end() ? nullptr : &*match;
}

void Thumb::drawItem(const LibraryIndex::Book& item, const int x, const int y, const int width,
                     const int height, const bool favorite) const {
  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
  const Thumbnail* thumbnail = find(item.path);
  if (item.type == LibraryIndex::Book::Type::FOLDER) {
    const bool hideTitle = SETTINGS.hideThumbnailTitles != 0;
    const bool inlineCount = layout.inlineFolderCount && !hideTitle;
    constexpr bool evenThumbnails = true;
    constexpr int padding = 3;
    const int lineHeight = hideTitle ? 0 : renderer.text.getLineHeight(systemFontId());
    const int titleHeight = hideTitle ? 0 : lineHeight * layout.titleLines + kFolderLabelGap;
    const int imageY = y + padding;
    const int imageHeight = std::max(8, height - titleHeight - padding * 2);
    const std::string liveFolderImage =
        thumbnail && !thumbnail->image.empty() && SdMan.exists(thumbnail->image.c_str())
            ? thumbnail->image
            : folderImagePath(item.path);
    if (thumbnail && thumbnail->loaded && !thumbnail->first.empty() &&
        SdMan.exists(thumbnail->first.c_str())) {
      const int imageWidth = std::max(12, width - padding * 2);
      cover(renderer, thumbnail->first, x + padding, imageY, imageWidth, imageHeight, rounded, true, true);
      if (!hideTitle) {
        if (inlineCount) {
          folderLabelWithCount(renderer, folderTitle(item), x + padding, imageWidth, y, height,
                               thumbnail->bookCount + thumbnail->folderCount);
        } else {
          folderLabel(renderer, folderTitle(item), x + padding, imageWidth, y, height, evenThumbnails ? 5 : 10);
        }
      }
      drawFavoriteBadge(renderer, x + padding, imageY, imageWidth, favorite, false);
      if (!inlineCount) drawFolderBookCountBadge(renderer, x + padding, imageY, imageWidth, imageHeight, thumbnail->bookCount);
      return;
    }

    const bool singleBook = thumbnail && thumbnail->bookCount == 1;
    if (!liveFolderImage.empty()) {
      const int imageWidth = std::max(12, width - padding * 2);
      cover(renderer, liveFolderImage, x + padding, imageY, imageWidth, imageHeight, rounded, true, true);
      if (!hideTitle) {
        const std::string title = folderTitle(item);
        if (inlineCount) {
          folderLabelWithCount(renderer, title, x + padding, imageWidth, y, height,
                               thumbnail->bookCount + thumbnail->folderCount);
        } else {
          folderLabel(renderer, title, x + padding, imageWidth, y, height, evenThumbnails ? 5 : 10);
        }
      }
      drawFavoriteBadge(renderer, x + padding, imageY, imageWidth, favorite, false);
      if (!singleBook && !inlineCount) {
        drawFolderBookCountBadge(renderer, x + padding, imageY, imageWidth, imageHeight, thumbnail->bookCount);
      }
      return;
    }

    const int count = thumbnail ? thumbnail->bookCount : folderBookCount(item, books);
    const int folderCount = thumbnail ? thumbnail->folderCount : folderChildCount(item.path, books);
    drawFolderPlaceholder(renderer, item, x, y, width, height, favorite, count, folderCount, {}, inlineCount);
    return;
  }

  const std::string liveBookImage =
      thumbnail && !thumbnail->image.empty() && SdMan.exists(thumbnail->image.c_str())
          ? thumbnail->image
          : imagePathForBook(item.path);
  if (!liveBookImage.empty()) {
    constexpr int padding = 3;
    const bool hideTitle = SETTINGS.hideThumbnailTitles != 0;
    const int lineHeight = hideTitle ? 0 : renderer.text.getLineHeight(systemFontId());
    const int titleHeight = hideTitle ? 0 : lineHeight * layout.titleLines + kFolderLabelGap;
    const int imageAreaY = y + padding;
    const int imageAreaWidth = std::max(8, width - padding * 2);
    const int imageAreaHeight = std::max(8, height - titleHeight - padding * 2);
    const int imageX = x + padding;
    const int imageY = imageAreaY;
    const int imageWidth = imageAreaWidth;
    const int imageHeight = imageAreaHeight;
    renderer.rectangle.fill(x + padding, imageAreaY, imageAreaWidth, imageAreaHeight, false, rounded);
    if (cover(renderer, liveBookImage, imageX, imageY, imageWidth, imageHeight, rounded, true, true)) {
      if (!hideTitle) {
        if (layout.titleLines > 1) {
          bookLabelWithProgress(renderer, bookTitle(item), x + padding, imageAreaWidth, imageAreaY, imageAreaHeight,
                                thumbnail->progressPercent);
        } else {
          folderLabel(renderer, bookTitle(item), x + padding, imageAreaWidth, y, height);
        }
      }
      drawFavoriteBadge(renderer, imageX, imageY, imageWidth, favorite);
      drawItemBadge(renderer, imageX, imageY, imageWidth, imageHeight, item.badge);
      return;
    }
  }
  constexpr int padding = 3;
  const bool hideTitle = SETTINGS.hideThumbnailTitles != 0;
  const int lineHeight = hideTitle ? 0 : renderer.text.getLineHeight(systemFontId());
  const int titleHeight = hideTitle ? 0 : lineHeight * layout.titleLines + kFolderLabelGap;
  const int imageAreaY = y + padding;
  const int imageAreaHeight = std::max(8, height - titleHeight - padding * 2);
  const int placeholderWidth = std::max(8, width - padding * 2);
  const int placeholderX = x + (width - placeholderWidth) / 2;
  placeholder(renderer, placeholderX, imageAreaY, placeholderWidth, imageAreaHeight,
              hideTitle ? bookTitle(item) : "");
  if (!hideTitle) {
    const int labelWidth = std::max(8, width - padding * 2);
    if (layout.titleLines > 1) {
      bookLabelWithProgress(renderer, bookTitle(item), x + padding, labelWidth, imageAreaY, imageAreaHeight,
                            thumbnail ? thumbnail->progressPercent : 0);
    } else {
      folderLabel(renderer, bookTitle(item), x + padding, labelWidth, y, height);
    }
  }
  drawFavoriteBadge(renderer, placeholderX, imageAreaY, placeholderWidth, favorite);
  drawItemBadge(renderer, placeholderX, imageAreaY, placeholderWidth, imageAreaHeight, item.badge);
}

void Thumb::itemBounds(const int index, int& x, int& y, int& width, int& height) const {
  const Rect& cell = cells[static_cast<size_t>(index)];
  x = cell.x;
  y = cell.y;
  width = cell.width;
  height = cell.height;
}

int Thumb::itemAt(const int x, const int y) const {
  const int perPage = itemsPerPage();
  for (int index = 0; index < std::min(perPage, static_cast<int>(items.size()) - page * perPage); ++index) {
    int itemX = 0;
    int itemY = 0;
    int itemWidth = 0;
    int itemHeight = 0;
    itemBounds(index, itemX, itemY, itemWidth, itemHeight);
    if (x >= itemX && x < itemX + itemWidth && y >= itemY && y < itemY + itemHeight) {
      return page * perPage + index;
    }
  }
  return -1;
}

bool Thumb::handleInput() {
  if (mappedInput.wasTouchSwipeUp()) {
    page = page + 1 < pageCount() ? page + 1 : 0;
    load();
    return true;
  }
  if (mappedInput.wasTouchSwipeDown()) {
    if (page > 0) {
      --page;
      load();
    }
    return true;
  }

  if (!mappedInput.hasTouch()) return false;

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) return false;

  const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
  const int index = itemAt(tapX, tapY);
  if (index >= 0) {
    if (select) select(index, mappedInput.lastTouchHeldMs() >= 500);
    return true;
  }
  if (outsideTap) outsideTap(tapX, tapY);
  return true;
}

void Thumb::render() const {
  const int perPage = itemsPerPage();
  const int start = page * perPage;
  const int count = std::min(perPage, static_cast<int>(items.size()) - start);
  for (int index = 0; index < count; ++index) {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    itemBounds(index, x, y, width, height);
    const LibraryIndex::Book& item = items[static_cast<size_t>(start + index)];
    const bool favorite = isFavorite && isFavorite(item);
    drawItem(item, x, y, width, height, favorite);
  }
}

}
}

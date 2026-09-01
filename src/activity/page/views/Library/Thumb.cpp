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
#include "images/Star.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/SdIoMutex.h"
#include "../../navigation/Menu.h"

namespace views {
namespace library {

namespace {

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

void drawTitle(const GfxRenderer& renderer, const std::string& value, const int x, const int y, const int width,
               const int font, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const std::string title = renderer.text.truncate(font, value.c_str(), std::max(8, width), style);
  renderer.text.render(font, x, y, title.c_str(), true, style);
}

void drawFavoriteBadge(const GfxRenderer& renderer, const int x, const int y, const int width, const bool favorite) {
  if (!favorite) return;
  constexpr int iconSize = 24;
  constexpr int badgePadding = 5;
  constexpr int badgeMargin = 5;
  constexpr int badgeSize = iconSize + badgePadding * 2;
  const int badgeX = x + std::max(0, width - badgeSize - badgeMargin);
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
  const int badgeY = y + std::max(0, height - badgeHeight - margin);
  renderer.rectangle.fill(badgeX, badgeY, badgeWidth, badgeHeight,
                          static_cast<int>(GfxRenderer::FillTone::Ink), true);
  renderer.rectangle.render(badgeX, badgeY, badgeWidth, badgeHeight, false, true);
  renderer.text.render(font, badgeX + paddingX, badgeY + paddingY, label.c_str(), false);
}

std::string parent(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return "/";
  return path.substr(0, slash);
}

std::string cacheDirectory(const std::string& bookPath) {
  std::string extension = bookPath;
  std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  const bool xtc = extension.size() >= 4 && extension.substr(extension.size() - 4) == ".xtc";
  const char* roots[] = {xtc ? "/.metadata/xtc" : "/.metadata/epub",
                         xtc ? "/.metadata/epub" : "/.metadata/xtc"};
  const char* names[] = {"thumb.jpg", "thumb.png", "thumb.bmp"};
  for (const char* root : roots) {
    const std::string directory = std::string(root) + "/" + std::to_string(std::hash<std::string>{}(bookPath));
    for (const char* name : names) {
      if (SdMan.exists((directory + "/" + name).c_str())) return directory;
    }
  }
  return {};
}

std::string imagePath(const std::string& directory) {
  if (directory.empty()) return {};
  const char* names[] = {"thumb.jpg", "thumb.png", "thumb.bmp"};
  for (const char* name : names) {
    const std::string path = directory + "/" + name;
    if (SdMan.exists(path.c_str())) return path;
  }
  return {};
}

std::string folderImagePath(const std::string& folder) {
  const char* names[] = {"thumb.jpg", "thumb.png", "thumb.bmp"};
  const std::string base = folder == "/" ? std::string() : folder;
  for (const char* name : names) {
    const std::string path = base + "/" + name;
    if (SdMan.exists(path.c_str())) return path;
  }
  return imagePath(cacheDirectory(folder));
}

void drawContrastFrame(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                       const bool rounded) {
  if (width < 1 || height < 1) return;
  renderer.rectangle.render(x, y, width, height, true, rounded);
  if (width > 3 && height > 3) {
    renderer.rectangle.render(x + 1, y + 1, width - 2, height - 2, false, rounded);
  }
}

std::vector<std::string> folderCovers(const std::string& folder, const std::vector<LibraryIndex::Book>& books,
                                      const int limit) {
  std::vector<std::string> covers;
  int checked = 0;
  for (const LibraryIndex::Book& book : books) {
    if (book.type != LibraryIndex::Book::Type::BOOK || parent(book.path) != folder) continue;
    if (checked++ == 32) break;
    const std::string path = imagePath(cacheDirectory(book.path));
    if (path.empty()) continue;
    covers.push_back(path);
    if (static_cast<int>(covers.size()) == limit) break;
  }
  if (!covers.empty()) return covers;

  const std::string prefix = folder == "/" ? folder : folder + "/";
  checked = 0;
  for (const LibraryIndex::Book& book : books) {
    if (book.type != LibraryIndex::Book::Type::BOOK) continue;
    if (book.path.compare(0, prefix.size(), prefix) != 0) continue;
    if (checked++ == 64) break;
    const std::string path = imagePath(cacheDirectory(book.path));
    if (path.empty()) continue;
    covers.push_back(path);
    if (static_cast<int>(covers.size()) == limit) break;
  }
  return covers;
}

int folderBookCount(const std::string& folder, const std::vector<LibraryIndex::Book>& books) {
  const std::string prefix = folder == "/" ? "/" : folder + "/";
  return static_cast<int>(std::count_if(books.begin(), books.end(), [&prefix](const LibraryIndex::Book& book) {
    return book.type == LibraryIndex::Book::Type::BOOK && book.path.compare(0, prefix.size(), prefix) == 0;
  }));
}

int folderBookCount(const LibraryIndex::Book& folder, const std::vector<LibraryIndex::Book>& books) {
  return books.empty() && folder.hasMetadata ? folder.bookCount : folderBookCount(folder.path, books);
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

bool coverSlice(GfxRenderer& renderer, const std::string& path, const int x, const int y, const int width,
                const int height, const bool rounded) {
  if (path.empty() || width < 1 || height < 8) return false;
  renderer.rectangle.fill(x, y, width, height, false, rounded);
  ImageRender::Options options;
  options.cropToFill = true;
  options.cropAnchorX = 0.0f;
  options.useDisplayCache = true;
  options.asyncDisplayCache = true;
  options.roundedOutside = rounded ? BitmapRender::RoundedOutside::PaperOutside
                                   : BitmapRender::RoundedOutside::None;
  if (!ImageRender::create(renderer, path).render(x, y, width, height, options)) return false;
  drawContrastFrame(renderer, x, y, width, height, rounded);
  return true;
}

void drawFolderStack(GfxRenderer& renderer, const std::string& firstPath, const std::string& secondPath,
                     const std::string& thirdPath,
                     const int frontX, const int frontY, const int frontWidth, const int frontHeight,
                     const bool rounded) {
  constexpr int layerStep = 7;

  const int backingHeight = std::max(8, frontHeight - 18);
  const int thirdHeight = std::max(8, frontHeight - 12);
  const int secondHeight = std::max(8, frontHeight - 6);
  renderer.rectangle.fill(frontX - layerStep * 2, frontY + (frontHeight - backingHeight) / 2, frontWidth,
                          backingHeight, static_cast<int>(GfxRenderer::FillTone::Paper), rounded);
  renderer.rectangle.render(frontX - layerStep * 2, frontY + (frontHeight - backingHeight) / 2, frontWidth,
                            backingHeight, true, rounded);
  if (!thirdPath.empty()) {
    coverSlice(renderer, thirdPath, frontX - layerStep * 2,
               frontY + (frontHeight - thirdHeight) / 2, layerStep * 2, thirdHeight, rounded);
  }
  if (!secondPath.empty()) {
    coverSlice(renderer, secondPath, frontX - layerStep, frontY + (frontHeight - secondHeight) / 2, layerStep,
               secondHeight, rounded);
  }
  cover(renderer, firstPath, frontX, frontY, frontWidth, frontHeight, rounded);
}

constexpr int kFolderLabelGap = 10;

void folderLabel(GfxRenderer& renderer, const std::string& value, const int labelX, const int labelWidthAvailable,
                 const int y, const int height) {
  const int font = systemFontId();
  const int lineHeight = renderer.text.getLineHeight(font);
  const std::string label = renderer.text.truncate(font, value.c_str(), std::max(8, labelWidthAvailable));
  renderer.text.render(font, labelX, y + height - lineHeight - 10, label.c_str(), true);
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
                           const std::string& titleOverride = {}) {
  constexpr int padding = 5;
  const bool hideTitle = SETTINGS.hideThumbnailTitles != 0;
  const int font = systemFontId();
  const int lineHeight = hideTitle ? 0 : renderer.text.getLineHeight(font);
  const int imageY = y + padding;
  const int labelSpacing = hideTitle ? 0 : 4 + kFolderLabelGap;
  const int imageHeight = std::max(8, height - lineHeight - padding * 2 - labelSpacing);
  const int emptyWidth = std::max(8, std::min(width - padding * 2, imageHeight * 2 / 3));
  const int emptyX = x + (width - emptyWidth) / 2;
  const std::string title = titleOverride.empty() ? bookTitle(item) : titleOverride;
  placeholder(renderer, emptyX, imageY, emptyWidth, imageHeight, hideTitle ? title : "");
  if (!hideTitle) {
    folderLabel(renderer, title, emptyX, emptyWidth, y, height);
  }
  drawFavoriteBadge(renderer, emptyX, imageY, emptyWidth, favorite);
  drawFolderBookCountBadge(renderer, emptyX, imageY, emptyWidth, imageHeight, bookCount);
}

}

Thumb::Thumb(GfxRenderer& renderer, MappedInputManager& mappedInput,
             const std::vector<LibraryIndex::Book>& items, const std::vector<LibraryIndex::Book>& books,
             std::function<void(int, bool)> select,
             std::function<bool(const LibraryIndex::Book&)> isFavorite,
             std::function<void(int, int)> outsideTap,
             std::function<bool(const LibraryIndex::Book&)> isAuthorFolder)
    : renderer(renderer), mappedInput(mappedInput), items(items), books(books), select(std::move(select)),
      isFavorite(std::move(isFavorite)),
      outsideTap(std::move(outsideTap)), isAuthorFolder(std::move(isAuthorFolder)) {}

void Thumb::getThumbnailSize(GfxRenderer& renderer, int& width, int& height) {
  const int availableWidth = renderer.getScreenWidth() - sideMargin * 2;
  const int availableHeight = renderer.getScreenHeight() - navigation::Menu::height -
                              navigation::Menu::bottomHeight - margin * 2;
  const int cellWidth = (availableWidth - gap * (childColumns - 1)) / childColumns;
  const int cellHeight = (availableHeight - rowGap * (childRows - 1)) / childRows;
  width = std::max(40, cellWidth);
  height = std::max(40, cellHeight);
}

void Thumb::setRoot(const bool value) { root = value; }

int Thumb::itemsPerPage() const { return childItemsPerPage; }

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
    if (item.type == LibraryIndex::Book::Type::FOLDER) {
      thumbnail.bookCount = folderBookCount(item, books);
      thumbnail.folderCount = item.hasMetadata ? item.folderCount : folderChildCount(item.path, books);
      if (thumbnail.bookCount == 1) {
        if (const LibraryIndex::Book* book = singleBookInFolder(item.path, books)) {
          thumbnail.singleBookTitle = bookTitle(*book);
        }
      }
    }
    const auto cached = resolvedCache_.find(item.path);
    if (cached != resolvedCache_.end()) {
      const int bookCount = thumbnail.bookCount;
      const int folderCount = thumbnail.folderCount;
      const std::string singleBookTitle = thumbnail.singleBookTitle;
      thumbnail = cached->second;
      thumbnail.item = item.path;
      thumbnail.bookCount = bookCount;
      thumbnail.folderCount = folderCount;
      if (!singleBookTitle.empty()) thumbnail.singleBookTitle = singleBookTitle;
      thumbnail.loaded = true;
    }
    thumbnails.push_back(std::move(thumbnail));
  }

  {
    std::array<Thumbnail*, childItemsPerPage> booksToMeasure{};
    int bookCount = 0;
    {
      SdIoMutex::Lock lock;
      for (Thumbnail& thumbnail : thumbnails) {
        if (thumbnail.loaded) continue;
        const auto item = std::find_if(items.begin(), items.end(), [&thumbnail](const LibraryIndex::Book& book) {
          return book.path == thumbnail.item;
        });
        if (item != items.end()) {
          if (item->type == LibraryIndex::Book::Type::FOLDER) {
            const std::vector<std::string> covers = folderCovers(item->path, books, 3);
            if (!covers.empty()) thumbnail.first = covers[0];
            if (covers.size() > 1) thumbnail.second = covers[1];
            if (covers.size() > 2) thumbnail.third = covers[2];
            if (thumbnail.first.empty()) thumbnail.image = folderImagePath(item->path);
            if (thumbnail.bookCount == 1) {
              if (const LibraryIndex::Book* book = singleBookInFolder(item->path, books)) {
                thumbnail.singleBookTitle = bookTitle(*book);
              }
            }
          } else {
            thumbnail.image = imagePath(cacheDirectory(item->path));
            if (!thumbnail.image.empty() && bookCount < static_cast<int>(booksToMeasure.size())) {
              booksToMeasure[bookCount++] = &thumbnail;
            }
          }
        }
        thumbnail.loaded = true;
      }
    }
    for (int index = 0; index < bookCount; ++index) {
      Thumbnail* thumbnail = booksToMeasure[static_cast<size_t>(index)];
      ImageRender::getDimensions(thumbnail->image, &thumbnail->imageWidth, &thumbnail->imageHeight);
    }
    for (Thumbnail& thumbnail : thumbnails) {
      if (thumbnail.loaded) resolvedCache_[thumbnail.item] = thumbnail;
    }
  }
}

bool Thumb::loadNext() {
  const unsigned long now = millis();
  if (mappedInput.isTouchPressed()) {
    loadAt = now + 20;
    return false;
  }
  if (static_cast<long>(now - loadAt) < 0) return false;

  const int limit = childItemsPerPage;
  int loaded = 0;
  std::array<Thumbnail*, childItemsPerPage> booksToMeasure{};
  int bookCount = 0;
  std::array<Thumbnail*, childItemsPerPage> resolvedThisCall{};
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
          const std::vector<std::string> covers = folderCovers(item->path, books, 3);
          if (!covers.empty()) thumbnail.first = covers[0];
          if (covers.size() > 1) thumbnail.second = covers[1];
          if (covers.size() > 2) thumbnail.third = covers[2];
          if (thumbnail.first.empty()) thumbnail.image = folderImagePath(item->path);
          if (thumbnail.bookCount == 1) {
            if (const LibraryIndex::Book* book = singleBookInFolder(item->path, books)) {
              thumbnail.singleBookTitle = bookTitle(*book);
            }
          }
        } else {
          thumbnail.image = imagePath(cacheDirectory(item->path));
          if (!thumbnail.image.empty() && bookCount < static_cast<int>(booksToMeasure.size())) {
            booksToMeasure[bookCount++] = &thumbnail;
          }
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
        const std::vector<std::string> covers = folderCovers(item.path, books, 3);
        if (!covers.empty()) thumbnail.first = covers[0];
        if (covers.size() > 1) thumbnail.second = covers[1];
        if (covers.size() > 2) thumbnail.third = covers[2];
        if (thumbnail.first.empty()) thumbnail.image = folderImagePath(item.path);
        thumbnail.bookCount = folderBookCount(item, books);
        thumbnail.folderCount = item.hasMetadata ? item.folderCount : folderChildCount(item.path, books);
        if (thumbnail.bookCount == 1) {
          if (const LibraryIndex::Book* book = singleBookInFolder(item.path, books)) {
            thumbnail.singleBookTitle = bookTitle(*book);
          }
        }
      } else {
        thumbnail.image = imagePath(cacheDirectory(item.path));
      }
    }
    if (item.type != LibraryIndex::Book::Type::FOLDER && !thumbnail.image.empty()) {
      ImageRender::getDimensions(thumbnail.image, &thumbnail.imageWidth, &thumbnail.imageHeight);
    }
    thumbnail.loaded = true;
    resolvedCache_[thumbnail.item] = thumbnail;
    return true;
  }
  return false;
}

void Thumb::reset() { page = 0; }

int Thumb::pageCount() const {
  const int perPage = itemsPerPage();
  return std::max(1, (static_cast<int>(items.size()) + perPage - 1) / perPage);
}

int Thumb::top() const { return navigation::Menu::height; }

void Thumb::calculate() {
  const int availableWidth = renderer.getScreenWidth() - sideMargin * 2;
  const int availableHeight = renderer.getScreenHeight() - top() - navigation::Menu::bottomHeight - margin * 2;
  const int width = std::max(40, (availableWidth - gap * (childColumns - 1)) / childColumns);
  const int height = std::max(40, (availableHeight - rowGap * (childRows - 1)) / childRows);
  const int gridWidth = width * childColumns + gap * (childColumns - 1);
  const int gridHeight = height * childRows + rowGap * (childRows - 1);
  const int x = sideMargin + std::max(0, (availableWidth - gridWidth) / 2);
  const int y = top() + margin + std::max(0, (availableHeight - gridHeight) / 2);

  for (int index = 0; index < childItemsPerPage; ++index) {
    Rect& cell = cells[static_cast<size_t>(index)];
    cell.width = width;
    cell.height = height;
    cell.x = x + (index % childColumns) * (width + gap);
    cell.y = y + (index / childColumns) * (height + rowGap);
  }
}

const Thumb::Thumbnail* Thumb::find(const std::string& item) const {
  const auto match = std::find_if(thumbnails.begin(), thumbnails.end(), [&item](const Thumbnail& thumbnail) {
    return thumbnail.item == item;
  });
  return match == thumbnails.end() ? nullptr : &*match;
}

void Thumb::drawItem(const LibraryIndex::Book& item, const int x, const int y, const int width,
                     const int height, const bool favorite, const bool authorFolder) const {
  constexpr int padding = 5;
  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
  const Thumbnail* thumbnail = find(item.path);
  if (item.type == LibraryIndex::Book::Type::FOLDER) {
    const bool hideTitle = SETTINGS.hideThumbnailTitles != 0;
    const int font = systemFontId();
    const int lineHeight = hideTitle ? 0 : renderer.text.getLineHeight(font);
    const int imageY = y + padding;
    const int labelSpacing = hideTitle ? 0 : 4 + kFolderLabelGap;
    const int imageHeight = std::max(8, height - lineHeight - padding * 2 - labelSpacing);
    if (authorFolder) {
      drawFolderPlaceholder(renderer, item, x, y, width, height, favorite, thumbnail ? thumbnail->bookCount : 0);
      return;
    }
    if (thumbnail && thumbnail->loaded && !thumbnail->first.empty()) {
      const int frontHeight = imageHeight;
      if (thumbnail->bookCount == 1) {
        const int coverWidth = std::max(12, std::min(width - padding * 2, frontHeight * 2 / 3));
        const int coverX = x + (width - coverWidth) / 2;
        cover(renderer, thumbnail->first, coverX, imageY, coverWidth, frontHeight, rounded);
        drawFavoriteBadge(renderer, coverX, imageY, coverWidth, favorite);
        if (!hideTitle) {
          folderLabel(renderer, thumbnail->singleBookTitle.empty() ? bookTitle(item) : thumbnail->singleBookTitle,
                      coverX, coverWidth, y, height);
        }
        return;
      }
      constexpr int layerStep = 7;
      const int availableWidth = std::max(24, width - padding * 2);
      const int frontWidth = std::max(12, std::min(availableWidth - layerStep * 3, frontHeight * 2 / 3));
      const int stackWidth = frontWidth + layerStep * 3;
      const int stackX = x + std::max(padding, (width - stackWidth) / 2);
      const int frontX = stackX + layerStep * 3;
      drawFolderStack(renderer, thumbnail->first, thumbnail->second, thumbnail->third, frontX, imageY, frontWidth,
                      frontHeight, rounded);
      if (!hideTitle) {
        folderLabel(renderer, bookTitle(item), frontX, frontWidth, y, height);
      }
      drawFavoriteBadge(renderer, frontX, imageY, frontWidth, favorite);
      drawFolderBookCountBadge(renderer, frontX, imageY, frontWidth, frontHeight, thumbnail->bookCount);
      return;
    }

    const bool singleBook = thumbnail && thumbnail->bookCount == 1;
    const std::string singleBookTitle = singleBook ? thumbnail->singleBookTitle : std::string();

    if (thumbnail && thumbnail->loaded &&
        cover(renderer, thumbnail->image, x + padding, imageY, width - padding * 2, imageHeight, rounded)) {
      if (!hideTitle) {
        folderLabel(renderer, singleBookTitle.empty() ? bookTitle(item) : singleBookTitle, x + padding,
                    width - padding * 2, y, height);
      }
      drawFavoriteBadge(renderer, x + padding, imageY, width - padding * 2, favorite);
      if (!singleBook) {
        drawFolderBookCountBadge(renderer, x + padding, imageY, width - padding * 2, imageHeight,
                                 thumbnail->bookCount);
      }
      return;
    }

    drawFolderPlaceholder(renderer, item, x, y, width, height, favorite,
                          singleBook ? 0 : (thumbnail ? thumbnail->bookCount : 0), singleBookTitle);
    return;
  }

  if (thumbnail && thumbnail->loaded) {
    const int font = systemFontId();
    const bool hideTitle = SETTINGS.hideThumbnailTitles != 0;
    const int lineHeight = hideTitle ? 0 : renderer.text.getLineHeight(font);
    constexpr int titleGap = 4;
    const int titleHeight = hideTitle ? 0 : lineHeight + titleGap;
    const int imageAreaY = y + padding;
    const int imageAreaWidth = std::max(8, width - padding * 2);
    const int imageAreaHeight = std::max(8, height - titleHeight - padding * 2);
    int imageX = x + padding;
    int imageY = imageAreaY;
    int imageWidth = imageAreaWidth;
    int imageHeight = imageAreaHeight;
    const bool hasSize = thumbnail->imageWidth > 0 && thumbnail->imageHeight > 0;
    const bool evenThumbnails = SETTINGS.thumbnailSize == SystemSetting::THUMBNAIL_EVEN;
    if (hasSize && !evenThumbnails) {
      const float scale = std::min(static_cast<float>(imageAreaWidth) / thumbnail->imageWidth,
                                   static_cast<float>(imageAreaHeight) / thumbnail->imageHeight);
      imageWidth = std::max(1, static_cast<int>(std::lround(thumbnail->imageWidth * scale)));
      imageHeight = std::max(1, static_cast<int>(std::lround(thumbnail->imageHeight * scale)));
      imageX += (imageAreaWidth - imageWidth) / 2;
      imageY += imageAreaHeight - imageHeight;
    }
    renderer.rectangle.fill(x + padding, imageAreaY, imageAreaWidth, imageAreaHeight, false, rounded);
    if (cover(renderer, thumbnail->image, imageX, imageY, imageWidth, imageHeight, rounded,
              hasSize || evenThumbnails, evenThumbnails)) {
      if (!hideTitle) drawTitle(renderer, bookTitle(item), imageX, y + height - titleHeight, imageWidth, font);
      drawFavoriteBadge(renderer, imageX, imageY, imageWidth, favorite);
      return;
    }
  }
  const int font = systemFontId();
  const bool hideTitle = SETTINGS.hideThumbnailTitles != 0;
  const int lineHeight = hideTitle ? 0 : renderer.text.getLineHeight(font);
  constexpr int titleGap = 4;
  const int titleHeight = hideTitle ? 0 : lineHeight + titleGap;
  const int imageAreaY = y + padding;
  const int imageAreaHeight = std::max(8, height - titleHeight - padding * 2);
  const int placeholderWidth = std::max(8, std::min(width - padding * 2, imageAreaHeight * 2 / 3));
  const int placeholderX = x + (width - placeholderWidth) / 2;
  placeholder(renderer, placeholderX, imageAreaY, placeholderWidth, imageAreaHeight,
              hideTitle ? bookTitle(item) : "");
  if (!hideTitle) drawTitle(renderer, bookTitle(item), placeholderX, y + height - titleHeight, placeholderWidth, font);
  drawFavoriteBadge(renderer, placeholderX, imageAreaY, placeholderWidth, favorite);
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
    if (page + 1 < pageCount()) {
      ++page;
      load();
    }
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
    drawItem(item, x, y, width, height, favorite, isAuthorFolder && isAuthorFolder(item));
  }
}

}
}

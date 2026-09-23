#include "Library.h"

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

std::string parent(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return "/";
  return path.substr(0, slash);
}

std::string cacheDirectory(const std::string& bookPath) {
  std::string lower = bookPath;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  const bool xtc = lower.size() >= 4 && lower.substr(lower.size() - 4) == ".xtc";
  const char* roots[] = {xtc ? "/.metadata/xtc" : "/.metadata/epub",
                         xtc ? "/.metadata/epub" : "/.metadata/xtc"};
  const char* names[] = {"thumb.jpg", "thumb.png", "thumb.bmp"};
  const std::string hash = std::to_string(std::hash<std::string>{}(bookPath));
  for (const char* root : roots) {
    const std::string directory = std::string(root) + "/" + hash;
    for (const char* name : names) {
      if (SdMan.exists((directory + "/" + name).c_str())) return directory;
    }
  }
  return {};
}

std::string imagePath(const std::string& directory) {
  if (directory.empty()) return {};
  for (const char* name : {"thumb.jpg", "thumb.png", "thumb.bmp"}) {
    const std::string path = directory + "/" + name;
    if (SdMan.exists(path.c_str())) return path;
  }
  return {};
}

std::string imagePathForBook(const std::string& path) { return imagePath(cacheDirectory(path)); }

std::string titleForFolder(const std::string& path) {
  if (path.empty() || path == "/") return "Library";
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos || slash + 1 >= path.size() ? "Library" : path.substr(slash + 1);
}

bool renderCover(GfxRenderer& renderer, const std::string& path, const int x, const int y, const int width,
                 const int height, const bool rounded, const bool cropToFill = false) {
  if (path.empty() || width < 8 || height < 8) return false;
  renderer.rectangle.fill(x, y, width, height, false, rounded);
  ImageRender::Options options;
  options.cropToFill = cropToFill;
  options.cropAnchorX = cropToFill ? 0.0f : 0.5f;
  options.useDisplayCache = true;
  options.asyncDisplayCache = true;
  options.roundedOutside = rounded ? BitmapRender::RoundedOutside::PaperOutside
                                   : BitmapRender::RoundedOutside::None;
  if (!ImageRender::create(renderer, path).render(x, y, width, height, options)) return false;
  renderer.rectangle.render(x, y, width, height, true, rounded);
  if (width > 3 && height > 3) renderer.rectangle.render(x + 1, y + 1, width - 2, height - 2, false, rounded);
  return true;
}

bool renderCoverSlice(GfxRenderer& renderer, const std::string& path, const int x, const int y, const int width,
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
  renderer.rectangle.render(x, y, width, height, true, rounded);
  if (width > 3 && height > 3) renderer.rectangle.render(x + 1, y + 1, width - 2, height - 2, false, rounded);
  return true;
}

void renderFolderBookCountBadge(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                                const int bookCount) {
  if (bookCount <= 0 || width < 8 || height < 8) return;
  constexpr int paddingX = 6;
  constexpr int paddingY = 4;
  constexpr int margin = 5;
  const int font = systemFontId();
  const std::string label = "+" + std::to_string(bookCount);
  const int badgeWidth = renderer.text.getWidth(font, label.c_str()) + paddingX * 2;
  const int badgeHeight = renderer.text.getLineHeight(font) + paddingY * 2;
  const int badgeX = x + std::max(0, width - badgeWidth - margin);
  const int badgeY = y + std::max(0, height - badgeHeight - margin);
  renderer.rectangle.fill(badgeX, badgeY, badgeWidth, badgeHeight,
                          static_cast<int>(GfxRenderer::FillTone::Ink), true);
  renderer.rectangle.render(badgeX, badgeY, badgeWidth, badgeHeight, false, true);
  renderer.text.render(font, badgeX + paddingX, badgeY + paddingY, label.c_str(), false);
}

void renderStack(GfxRenderer& renderer, const std::vector<std::string>& paths, const int frontX, const int frontY,
                 const int frontWidth, const int frontHeight) {
  constexpr int layerStep = 7;
  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
  const int backingHeight = std::max(8, frontHeight - 18);
  const int thirdHeight = std::max(8, frontHeight - 12);
  const int secondHeight = std::max(8, frontHeight - 6);
  renderer.rectangle.fill(frontX - layerStep * 2, frontY + (frontHeight - backingHeight) / 2, frontWidth,
                          backingHeight, static_cast<int>(GfxRenderer::FillTone::Paper), rounded);
  renderer.rectangle.render(frontX - layerStep * 2, frontY + (frontHeight - backingHeight) / 2, frontWidth,
                            backingHeight, true, rounded);
  if (paths.size() > 2) {
    const int x = frontX - layerStep * 2;
    const int y = frontY + (frontHeight - thirdHeight) / 2;
    if (!renderCoverSlice(renderer, paths[2], x, y, layerStep * 2, thirdHeight, rounded))
      renderer.rectangle.render(x, y, layerStep * 2, thirdHeight, true, rounded);
  }
  if (paths.size() > 1) {
    const int x = frontX - layerStep;
    const int y = frontY + (frontHeight - secondHeight) / 2;
    if (!renderCoverSlice(renderer, paths[1], x, y, layerStep, secondHeight, rounded))
      renderer.rectangle.render(x, y, layerStep, secondHeight, true, rounded);
  }
  if (!paths.empty() && !renderCover(renderer, paths[0], frontX, frontY, frontWidth, frontHeight, rounded))
    renderer.rectangle.render(frontX, frontY, frontWidth, frontHeight, true, rounded);
}

}

void LibraryWidget::load() const {
  books_.clear();
  LibraryIndex::search("", books_, LibraryIndex::all);
  loaded_ = true;
}

void LibraryWidget::invalidate() const {
  loaded_ = false;
  books_.clear();
}

std::string LibraryWidget::folderName(const std::string& folder) const {
  if (!loaded_) load();
  for (const LibraryIndex::Book& entry : books_) {
    if (entry.type == LibraryIndex::Book::Type::FOLDER && entry.path == folder && !entry.title.empty()) {
      return entry.title;
    }
  }
  return titleForFolder(folder);
}

std::vector<LibraryWidget::Folder> LibraryWidget::folders() {
  std::vector<LibraryIndex::Book> entries;
  std::vector<Folder> result{{"/", "Library"}};
  if (!LibraryIndex::search("", entries, LibraryIndex::all)) return result;
  for (const LibraryIndex::Book& entry : entries) {
    if (entry.type != LibraryIndex::Book::Type::FOLDER || entry.path.empty() || entry.path == "/") continue;
    result.push_back({entry.path, entry.title.empty() ? titleForFolder(entry.path) : entry.title});
  }
  std::sort(result.begin() + 1, result.end(), [](const Folder& left, const Folder& right) {
    return left.name < right.name;
  });
  return result;
}

std::string LibraryWidget::folderImagePath(const std::string& folder) const {
  for (const char* name : {"thumb.jpg", "thumb.png", "thumb.bmp"}) {
    const std::string path = (folder == "/" ? std::string() : folder) + "/" + name;
    if (SdMan.exists(path.c_str())) return path;
  }
  return imagePath(cacheDirectory(folder));
}

std::vector<std::string> LibraryWidget::covers(const std::string& folder) const {
  if (!loaded_) load();
  std::vector<std::string> result;
  int checked = 0;
  for (const LibraryIndex::Book& book : books_) {
    if (book.type != LibraryIndex::Book::Type::BOOK || parent(book.path) != folder) continue;
    if (checked++ == 32) break;
    const std::string path = imagePathForBook(book.path);
    if (!path.empty()) result.push_back(path);
    if (result.size() == 3) return result;
  }
  const std::string prefix = folder == "/" ? "/" : folder + "/";
  checked = 0;
  for (const LibraryIndex::Book& book : books_) {
    if (book.type != LibraryIndex::Book::Type::BOOK || book.path.compare(0, prefix.size(), prefix) != 0) continue;
    if (checked++ == 64) break;
    const std::string path = imagePathForBook(book.path);
    if (!path.empty()) result.push_back(path);
    if (result.size() == 3) break;
  }
  return result;
}

int LibraryWidget::folderBookCount(const std::string& folder) const {
  if (!loaded_) load();
  const std::string prefix = folder == "/" ? "/" : folder + "/";
  return static_cast<int>(std::count_if(books_.begin(), books_.end(), [&prefix](const LibraryIndex::Book& book) {
    return book.type == LibraryIndex::Book::Type::BOOK && book.path.compare(0, prefix.size(), prefix) == 0;
  }));
}

void LibraryWidget::renderStack(const std::vector<std::string>& paths, const int x, const int y, const int width,
                                const int height) const {
  ::renderStack(renderer_, paths, x, y, std::max(12, width), std::max(8, height));
}

void LibraryWidget::renderFolder(const int x, const int y, const int width, const int height, const char* folder,
                                 const bool showLabel) const {
  if (width <= 0 || height <= 0) return;
  constexpr int padding = 5;
  const std::string selectedFolder = folder && folder[0] == '/' ? folder : "/";
  const int labelFont = systemFontId();
  const int labelHeight = showLabel ? renderer_.text.getLineHeight(labelFont) : 0;
  constexpr int folderLabelGap = 10;
  const int labelSpacing = showLabel ? 4 + folderLabelGap : 0;
  const int imageY = y + padding;
  const int imageHeight = std::max(8, height - labelHeight - padding * 2 - labelSpacing);
  int labelX = x + padding;
  int labelWidth = std::max(8, width - padding * 2);
  std::vector<std::string> paths = covers(selectedFolder);
  if (paths.empty()) {
    const std::string customImage = folderImagePath(selectedFolder);
    if (!customImage.empty()) paths.push_back(customImage);
  }
  if (!paths.empty()) {
    const int bookCount = folderBookCount(selectedFolder);
    if (paths.size() == 1 && bookCount <= 1) {
      const int coverWidth = std::max(12, std::min(width - padding * 2, imageHeight * 2 / 3));
      const int coverX = x + (width - coverWidth) / 2;
      renderCover(renderer_, paths[0], coverX, imageY, coverWidth, imageHeight,
                  SETTINGS.bitmapRoundedCorners != 0);
      labelX = coverX;
      labelWidth = coverWidth;
    } else {
      constexpr int layerStep = 7;
      const int availableWidth = std::max(24, width - padding * 2);
      const int frontWidth = std::max(12, std::min(availableWidth - layerStep * 3, imageHeight * 2 / 3));
      const int stackWidth = frontWidth + layerStep * 3;
      const int stackX = x + std::max(padding, (width - stackWidth) / 2);
      const int frontX = stackX + layerStep * 3;
      renderStack(paths, frontX, imageY, frontWidth, imageHeight);
      renderFolderBookCountBadge(renderer_, frontX, imageY, frontWidth, imageHeight, bookCount);
      labelX = frontX;
      labelWidth = frontWidth;
    }
  } else {
    renderer_.text.centered(labelFont, y + std::max(0, (imageHeight - renderer_.text.getLineHeight(labelFont)) / 2),
                            "No books");
  }
  if (showLabel) {
    const std::string label = folderName(selectedFolder);
    const int labelY = y + height - labelHeight - 10;
    const std::string shown = renderer_.text.truncate(labelFont, label.c_str(), labelWidth);
    renderer_.text.render(labelFont, labelX, labelY, shown.c_str(), true, EpdFontFamily::REGULAR);
  }
}

void LibraryWidget::renderFolders(const int x, const int y, const int width, const int height,
                                  const char (*folders)[128], const bool showLabel) const {
  constexpr int padding = 10;
  const int contentX = x + padding;
  const int contentY = y + padding;
  const int contentWidth = std::max(8, width - padding * 2);
  const int contentHeight = std::max(8, height - padding * 2);
  constexpr int columnGap = 10;
  const int folderWidth = std::max(8, (contentWidth - columnGap) / kVisibleFolderCount);
  for (int index = 0; index < kVisibleFolderCount; ++index) {
    const char* folder = folders && folders[index][0] == '/' ? folders[index] : "/";
    renderFolder(contentX + index * (folderWidth + columnGap), contentY, folderWidth, contentHeight, folder, showLabel);
  }
}

void LibraryWidget::render(const int x, const int y, const int width, const int height, const char (*folders)[128],
                           const bool background, const bool showLabel) const {
  if (width <= 0 || height <= 0) return;
  renderBackground(x, y, width, height, background);
  renderFolders(x, y, width, height, folders, showLabel);
}

void LibraryWidget::preview(const int x, const int y, const int width, const int height, const char (*folders)[128],
                            const bool background, const bool showLabel) const {
  render(x, y, width, height, folders, background, showLabel);
}

bool LibraryWidget::hitTest(const int x, const int y, const int areaX, const int areaY, const int areaW,
                            const int areaH) const {
  return x >= areaX && x < areaX + areaW && y >= areaY && y < areaY + areaH;
}

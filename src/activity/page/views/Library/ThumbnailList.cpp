#include "ThumbnailList.h"

#include <BitmapRender.h>
#include <GfxRenderer.h>
#include <ImageRender.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <utility>

#include "images/LibraryFilterRight.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "../../navigation/Menu.h"

namespace views {
namespace library {

namespace {

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

std::string folderFor(const LibraryIndex::Book& book) {
  std::string folder = book.folder;
  if (folder.empty()) {
    const size_t slash = book.path.find_last_of('/');
    folder = slash == std::string::npos || slash == 0 ? "/" : book.path.substr(0, slash);
  }
  return folder == "/" ? "Library" : folder;
}

std::string metadataImagePath(const std::string& bookPath) {
  const std::string extension = lower(bookPath);
  const bool xtc = extension.size() >= 4 && extension.compare(extension.size() - 4, 4, ".xtc") == 0;
  const char* roots[] = {xtc ? "/.metadata/xtc" : "/.metadata/epub",
                         xtc ? "/.metadata/epub" : "/.metadata/xtc"};
  const char* names[] = {"thumb.jpg", "thumb.png", "thumb.bmp"};
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
  const std::string base = folder == "/" ? std::string() : folder;
  const char* names[] = {"thumb.jpg", "thumb.png", "thumb.bmp"};
  for (const char* name : names) {
    const std::string path = base + "/" + name;
    if (SdMan.exists(path.c_str())) return path;
  }
  return {};
}

}

ThumbnailList::ThumbnailList(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const std::vector<LibraryIndex::Book>& items,
                             const std::vector<LibraryIndex::Book>& books,
                             std::function<void(int, bool)> select,
                             std::function<bool(const LibraryIndex::Book&)> isFavorite,
                             std::function<void(int, int)> outsideTap,
                             std::function<std::string(const LibraryIndex::Book&)> customThumbnail)
    : renderer(renderer), mappedInput(mappedInput), items(items), books(books), select(std::move(select)),
      isFavorite(std::move(isFavorite)), outsideTap(std::move(outsideTap)),
      customThumbnail(std::move(customThumbnail)) {}

void ThumbnailList::reset() { page = 0; }

int ThumbnailList::top() const { return navigation::Menu::height; }

int ThumbnailList::rowHeight() const {
  if (SETTINGS.hiddenMenu == 0) return normalRowHeight;
  const int available = std::max(1, renderer.getScreenHeight() - top());
  // Six rows still leave the 104px thumbnail enough vertical breathing room.
  return std::max(thumbnailHeight + 10, available / 6);
}

int ThumbnailList::visibleRows() const {
  const int available = renderer.getScreenHeight() - top() - navigation::Menu::bottomInset();
  return std::max(1, available / rowHeight());
}

int ThumbnailList::pageCount() const {
  const int rows = visibleRows();
  return std::max(1, (static_cast<int>(items.size()) + rows - 1) / rows);
}

int ThumbnailList::itemAt(const int x, const int y) const {
  (void)x;
  if (y < top()) return -1;
  const int row = (y - top()) / rowHeight();
  const int rows = visibleRows();
  if (row < 0 || row >= rows || y >= top() + rows * rowHeight()) return -1;
  const int index = page * rows + row;
  return index >= 0 && index < static_cast<int>(items.size()) ? index : -1;
}

bool ThumbnailList::handleInput() {
  if (mappedInput.wasTouchSwipeUp()) {
    if (page + 1 < pageCount()) ++page;
    return true;
  }
  if (mappedInput.wasTouchSwipeDown()) {
    if (page > 0) --page;
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

std::string ThumbnailList::titleFor(const LibraryIndex::Book& book) {
  if (!book.title.empty()) return book.title;
  const size_t slash = book.path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = book.path.find_last_of('.');
  const size_t end = dot == std::string::npos || dot < start ? book.path.size() : dot;
  return book.path.substr(start, end - start);
}

std::string ThumbnailList::thumbnailPathFor(const LibraryIndex::Book& book) const {
  if (customThumbnail) {
    const std::string custom = customThumbnail(book);
    if (!custom.empty()) return custom;
  }
  if (book.type == LibraryIndex::Book::Type::FOLDER) {
    const std::string folderThumbnail = folderImagePath(book.path);
    if (!folderThumbnail.empty()) return folderThumbnail;

    const std::string prefix = book.path == "/" ? "/" : book.path + "/";
    for (const LibraryIndex::Book& child : books) {
      if (child.type != LibraryIndex::Book::Type::BOOK ||
          child.path.compare(0, prefix.size(), prefix) != 0) {
        continue;
      }
      return metadataImagePath(child.path);
    }
  }
  return metadataImagePath(book.path);
}

void ThumbnailList::renderThumbnail(const LibraryIndex::Book& book, const int x, const int y) const {
  const std::string path = thumbnailPathFor(book);
  ImageRender::Options options;
  options.cropToFill = true;
  options.asyncDisplayCache = true;
  options.useDisplayCache = true;
  options.roundedOutside = SETTINGS.bitmapRoundedCorners != 0 ? BitmapRender::RoundedOutside::PaperOutside
                                                               : BitmapRender::RoundedOutside::None;
  if (!path.empty() && ImageRender::create(renderer, path).render(x + 2, y + 2, thumbnailWidth - 4,
                                                                   thumbnailHeight - 4, options)) {
    return;
  }

  renderer.rectangle.fill(x, y, thumbnailWidth, thumbnailHeight, false,
                          SETTINGS.bitmapRoundedCorners != 0);
  if (SETTINGS.bitmapRoundedCorners == 0) {
    renderer.rectangle.dotted(x, y, thumbnailWidth, thumbnailHeight, true);
  } else {
    renderer.rectangle.render(x, y, thumbnailWidth, thumbnailHeight, true, true);
  }
}

void ThumbnailList::render() const {
  const int rows = visibleRows();
  const int currentRowHeight = rowHeight();
  const int start = page * rows;
  const int end = std::min(static_cast<int>(items.size()), start + rows);
  const int width = renderer.getScreenWidth();
  const int titleFont = systemFontIdForPointSize(14);
  const int descriptionFont = systemFontIdForPointSize(10);
  const int caretX = width - sideMargin - caretSize;
  const int textX = sideMargin + thumbnailWidth + textGap;
  const int textWidth = std::max(40, caretX - textX - textGap);

  for (int index = start; index < end; ++index) {
    const int row = index - start;
    const int rowY = top() + row * currentRowHeight;
    const LibraryIndex::Book& book = items[static_cast<size_t>(index)];
    renderThumbnail(book, sideMargin, rowY + (currentRowHeight - thumbnailHeight) / 2);

    const std::string title = renderer.text.truncate(titleFont, titleFor(book).c_str(), textWidth,
                                                     EpdFontFamily::BOLD);
    const int titleY = rowY + 23;
    renderer.text.render(titleFont, textX, titleY, title.c_str(), true, EpdFontFamily::BOLD);

    const std::string description = renderer.text.truncate(descriptionFont, folderFor(book).c_str(), textWidth);
    const int descriptionY = titleY + renderer.text.getLineHeight(titleFont) + 12;
    renderer.text.render(descriptionFont, textX, descriptionY, description.c_str(), true,
                         EpdFontFamily::REGULAR);

    const int caretY = rowY + (currentRowHeight - caretSize) / 2;
    renderer.bitmap.iconScaled(LibraryFilterRight, caretX, caretY, 32, 30, caretSize, caretSize);
    if (index + 1 < end) {
      renderer.line.render(sideMargin, rowY + currentRowHeight - 1, width - sideMargin,
                           rowY + currentRowHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }
}

}
}

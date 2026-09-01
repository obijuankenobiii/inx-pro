#include "Grid.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

#include "images/BookLarge.h"
#include "images/FolderLarge.h"
#define FolderLarge FolderAuthorLarge
#include "images/FolderAuthorLarge.h"
#undef FolderLarge
#include "images/ImageLarge.h"
#include "images/Pdf72.h"
#include "images/Star.h"
#include "images/Txt72.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
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

bool isImage(const std::string& path) {
  std::string value = path;
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  const auto endsWith = [&value](const char* suffix) {
    const size_t length = std::char_traits<char>::length(suffix);
    return value.size() >= length && value.compare(value.size() - length, length, suffix) == 0;
  };
  return endsWith(".bmp") || endsWith(".jpg") || endsWith(".jpeg") || endsWith(".png");
}

bool isPdf(const std::string& path) {
  std::string value = path;
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value.size() >= 4 && value.compare(value.size() - 4, 4, ".pdf") == 0;
}

bool isTxt(const std::string& path) {
  std::string value = path;
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value.size() >= 4 && value.compare(value.size() - 4, 4, ".txt") == 0;
}

void titleLines(const GfxRenderer& renderer, const std::string& value, const int font, const int width,
                std::string& first, std::string& second) {
  first.clear();
  second.clear();
  if (value.empty()) return;

  if (renderer.text.getWidth(font, value.c_str()) <= width) {
    first = value;
    return;
  }

  size_t split = value.find(' ');
  size_t best = std::string::npos;
  while (split != std::string::npos) {
    const std::string candidate = value.substr(0, split);
    if (renderer.text.getWidth(font, candidate.c_str()) > width) break;
    best = split;
    split = value.find(' ', split + 1);
  }

  if (best == std::string::npos) {
    first = renderer.text.truncate(font, value.c_str(), width);
    return;
  }

  first = value.substr(0, best);
  while (best < value.size() && value[best] == ' ') ++best;
  second = renderer.text.truncate(font, value.substr(best).c_str(), width);
}

void drawItem(const GfxRenderer& renderer, const LibraryIndex::Book& item, const int x, const int y,
              const int width, const int height, const bool favorite, const bool authorFolder) {
  constexpr int labelGap = 4;
  constexpr int labelHeight = 36;
  const int iconX = x + 8;
  const int iconY = y + 8;
  const int iconWidth = std::max(8, width - 16);
  const int iconHeight = std::max(8, height - labelHeight - labelGap - 16);
  const int iconSize = std::min(72, std::max(32, std::min(iconWidth, iconHeight) - 12));
  const int drawX = iconX + (iconWidth - iconSize) / 2;
  const int drawY = iconY + (iconHeight - iconSize) / 2;
  const uint8_t* icon = item.type == LibraryIndex::Book::Type::FOLDER
                            ? (authorFolder ? FolderAuthorLarge : FolderLarge)
                            : (isImage(item.path)
                                  ? ImageLarge
                                  : (isPdf(item.path) ? Pdf72 : (isTxt(item.path) ? Txt72 : BookLarge)));
  renderer.bitmap.icon(icon, drawX, drawY, iconSize, iconSize);
  if (favorite) renderer.bitmap.icon(Star, x + width - 30, y + 6, 24, 24);
  const int font = systemFontId();
  const int available = std::max(20, width - 10);
  std::string first;
  std::string second;
  titleLines(renderer, bookTitle(item), font, available, first, second);
  const int lineHeight = renderer.text.getLineHeight(font);
  const int lineCount = second.empty() ? 1 : 2;
  const int labelY = iconY + iconHeight + labelGap + (labelHeight - lineCount * lineHeight) / 2;
  const int firstWidth = renderer.text.getWidth(font, first.c_str());
  renderer.text.render(font, x + (width - firstWidth) / 2, labelY, first.c_str(), true);
  if (!second.empty()) {
    const int secondWidth = renderer.text.getWidth(font, second.c_str());
    renderer.text.render(font, x + (width - secondWidth) / 2, labelY + lineHeight, second.c_str(), true);
  }
}

}

Grid::Grid(GfxRenderer& renderer, MappedInputManager& mappedInput,
           const std::vector<LibraryIndex::Book>& items, std::function<void(int, bool)> select,
           std::function<bool(const LibraryIndex::Book&)> isFavorite,
           std::function<void(int, int)> outsideTap,
           std::function<bool(const LibraryIndex::Book&)> isAuthorFolder)
    : renderer(renderer), mappedInput(mappedInput), items(items), select(std::move(select)),
      isFavorite(std::move(isFavorite)),
      outsideTap(std::move(outsideTap)), isAuthorFolder(std::move(isAuthorFolder)) {}

void Grid::reset() { page = 0; }

int Grid::top() const { return navigation::Menu::height; }

int Grid::visibleHeight() const {
  return std::max(1, renderer.getScreenHeight() - top() - navigation::Menu::bottomHeight - 10);
}

int Grid::pageCount() const {
  return std::max(1, (static_cast<int>(items.size()) + itemsPerPage - 1) / itemsPerPage);
}

void Grid::itemBounds(const int index, int& x, int& y, int& width, int& height) const {
  const int availableWidth = renderer.getScreenWidth() - margin * 2;
  const int availableHeight = visibleHeight();
  const int cellWidth = (availableWidth - gapX * (columns - 1)) / columns;
  const int cellHeight = (availableHeight - minGapY * (rows - 1)) / rows;
  width = std::min(maxFrame, cellWidth);
  height = std::max(96, std::min(maxFrame, cellHeight));
  const int remainingWidth = availableWidth - columns * width;
  const int actualGapX = std::max(gapX, remainingWidth / (columns - 1));
  const int remainingHeight = availableHeight - rows * height;
  const int actualGapY = std::max(minGapY, remainingHeight / (rows - 1));
  const int blockWidth = columns * width + (columns - 1) * actualGapX;
  const int blockHeight = rows * height + (rows - 1) * actualGapY;
  const int left = margin + std::max(0, (availableWidth - blockWidth) / 2);
  const int startY = top() + std::max(0, (availableHeight - blockHeight) / 2);
  x = left + (index % columns) * (width + actualGapX);
  y = startY + (index / columns) * (height + actualGapY);
}

int Grid::itemAt(const int x, const int y) const {
  const int count = std::min(itemsPerPage, static_cast<int>(items.size()) - page * itemsPerPage);
  for (int index = 0; index < count; ++index) {
    int itemX = 0;
    int itemY = 0;
    int itemWidth = 0;
    int itemHeight = 0;
    itemBounds(index, itemX, itemY, itemWidth, itemHeight);
    if (x >= itemX && x < itemX + itemWidth && y >= itemY && y < itemY + itemHeight) {
      return page * itemsPerPage + index;
    }
  }
  return -1;
}

bool Grid::handleInput() {
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

void Grid::render() const {
  const int start = page * itemsPerPage;
  const int count = std::min(itemsPerPage, static_cast<int>(items.size()) - start);
  for (int index = 0; index < count; ++index) {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    itemBounds(index, x, y, width, height);
    const LibraryIndex::Book& item = items[static_cast<size_t>(start + index)];
    drawItem(renderer, item, x, y, width, height, isFavorite && isFavorite(item),
             isAuthorFolder && isAuthorFolder(item));
  }
}

}
}

#include "List.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <string>
#include <utility>

#include "images/BookSmall.h"
#include "images/Folder.h"
#include "images/Pdf24.h"
#include "images/Star.h"
#include "images/Txt24.h"
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

}

List::List(GfxRenderer& renderer, MappedInputManager& mappedInput,
           const std::vector<LibraryIndex::Book>& items, std::function<void(int, bool)> select,
           std::function<bool(const LibraryIndex::Book&)> isFavorite,
           std::function<void(int, int)> outsideTap)
    : renderer(renderer), mappedInput(mappedInput), items(items), select(std::move(select)),
      isFavorite(std::move(isFavorite)),
      outsideTap(std::move(outsideTap)) {}

void List::reset() { page = 0; }

int List::top() const { return navigation::Menu::topPadding + navigation::Menu::iconSize; }

int List::availableHeight() const {
  const int bottomInset = SETTINGS.hiddenMenu != 0 ? UiLayout::LIBRARY_BOTTOM_BAR_HEIGHT
                                                   : navigation::Menu::bottomInset();
  return std::max(1, renderer.getScreenHeight() - top() - bottomInset);
}

int List::rowHeight() const { return std::max(1, availableHeight() / itemsPerPage); }

int List::pageCount() const {
  return std::max(1, (static_cast<int>(items.size()) + itemsPerPage - 1) / itemsPerPage);
}

int List::itemAt(const int x, const int y) const {
  (void)x;
  const int currentRowHeight = rowHeight();
  if (y < top() || y >= top() + itemsPerPage * currentRowHeight) return -1;
  const int row = (y - top()) / currentRowHeight;
  const int index = page * itemsPerPage + row;
  return index >= 0 && index < static_cast<int>(items.size()) ? index : -1;
}

bool List::handleInput() {
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

void List::render() const {
  const int start = page * itemsPerPage;
  const int count = std::min(itemsPerPage, static_cast<int>(items.size()) - start);
  const int width = renderer.getScreenWidth();
  const int font = systemFontId();
  const int currentRowHeight = rowHeight();
  for (int row = 0; row < count; ++row) {
    const int index = start + row;
    const int y = top() + row * currentRowHeight;
    const LibraryIndex::Book& item = items[static_cast<size_t>(index)];
    const bool favorite = isFavorite && isFavorite(item);
    const uint8_t* icon = item.type == LibraryIndex::Book::Type::FOLDER
                              ? Folder
                              : (isPdf(item.path) ? Pdf24 : (isTxt(item.path) ? Txt24 : BookSmall));
    renderer.bitmap.icon(icon, 20, y + (currentRowHeight - 24) / 2, 24, 24);
    constexpr int favoriteSize = 24;
    const int available = std::max(40, width - 54 - 20 - (favorite ? favoriteSize + 10 : 0));
    const std::string label = renderer.text.truncate(font, bookTitle(item).c_str(), available);
    renderer.text.render(font, 54, y + (currentRowHeight - renderer.text.getLineHeight(font)) / 2, label.c_str(), true);
    if (favorite) renderer.bitmap.icon(Star, width - 20 - favoriteSize, y + (currentRowHeight - favoriteSize) / 2,
                                       favoriteSize, favoriteSize);
    if (row + 1 < count) {
      renderer.line.render(20, y + currentRowHeight - 1, width - 20, y + currentRowHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }
}

}
}

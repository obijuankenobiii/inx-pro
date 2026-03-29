#pragma once

#include "system/UiLayout.h"

#include <functional>
#include <vector>

#include "util/LibraryIndex.h"

class GfxRenderer;
class MappedInputManager;

namespace views {
namespace library {

class List final {
 public:
  List(GfxRenderer& renderer, MappedInputManager& mappedInput,
       const std::vector<LibraryIndex::Book>& items, std::function<void(int, bool)> select,
       std::function<bool(const LibraryIndex::Book&)> isFavorite,
       std::function<void(int, int)> outsideTap,
       std::function<bool(const LibraryIndex::Book&)> isAuthorFolder);

  void reset();
  bool handleInput();
  void render() const;

 private:
  // Match the legacy library list: 66 px rows and 24 px leading icons.
  static constexpr int rowHeight = UiLayout::LIST_ITEM_HEIGHT;
  static constexpr int itemsPerPage = 10;

  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  const std::vector<LibraryIndex::Book>& items;
  std::function<void(int, bool)> select;
  std::function<bool(const LibraryIndex::Book&)> isFavorite;
  std::function<void(int, int)> outsideTap;
  std::function<bool(const LibraryIndex::Book&)> isAuthorFolder;
  int page = 0;

  int top() const;
  int visibleRows() const;
  int pageCount() const;
  int itemAt(int x, int y) const;
};

}  // namespace library
}  // namespace views

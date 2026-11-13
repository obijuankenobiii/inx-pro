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
       std::function<void(int, int)> outsideTap);

  void reset();
  bool handleInput();
  void render() const;
  int currentPage() const { return page; }
  int totalPages() const { return pageCount(); }

 private:
  static constexpr int itemsPerPage = 9;

  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  const std::vector<LibraryIndex::Book>& items;
  std::function<void(int, bool)> select;
  std::function<bool(const LibraryIndex::Book&)> isFavorite;
  std::function<void(int, int)> outsideTap;
  int page = 0;

  int top() const;
  int availableHeight() const;
  int rowHeight() const;
  int pageCount() const;
  int itemAt(int x, int y) const;
};

}
}

#pragma once

#include <functional>
#include <vector>

#include "util/LibraryIndex.h"

class GfxRenderer;
class MappedInputManager;

namespace views {
namespace library {

class Grid final {
 public:
  Grid(GfxRenderer& renderer, MappedInputManager& mappedInput,
       const std::vector<LibraryIndex::Book>& items, std::function<void(int, bool)> select,
       std::function<bool(const LibraryIndex::Book&)> isFavorite,
       std::function<void(int, int)> outsideTap,
       std::function<bool(const LibraryIndex::Book&)> isAuthorFolder);

  void reset();
  bool handleInput();
  void render() const;

 private:
  static constexpr int itemsPerPage = 12;
  static constexpr int columns = 3;
  static constexpr int rows = 4;
  // These are the legacy folder-grid dimensions.
  static constexpr int margin = 8;
  static constexpr int gapX = 8;
  static constexpr int minGapY = 6;
  static constexpr int labelGap = 4;
  static constexpr int labelHeight = 44;
  static constexpr int maxFrame = 148;

  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  const std::vector<LibraryIndex::Book>& items;
  std::function<void(int, bool)> select;
  std::function<bool(const LibraryIndex::Book&)> isFavorite;
  std::function<void(int, int)> outsideTap;
  std::function<bool(const LibraryIndex::Book&)> isAuthorFolder;
  int page = 0;

  int pageCount() const;
  int top() const;
  int visibleHeight() const;
  void itemBounds(int index, int& x, int& y, int& width, int& height) const;
  int itemAt(int x, int y) const;
};

}  // namespace library
}  // namespace views

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "util/LibraryIndex.h"

class GfxRenderer;
class MappedInputManager;

namespace views {
namespace library {

/** A Statistics-style book list with cover thumbnails and no progress column. */
class ThumbnailList final {
 public:
  ThumbnailList(GfxRenderer& renderer, MappedInputManager& mappedInput,
                const std::vector<LibraryIndex::Book>& items, const std::vector<LibraryIndex::Book>& books,
                std::function<void(int, bool)> select,
                std::function<bool(const LibraryIndex::Book&)> isFavorite,
                std::function<void(int, int)> outsideTap,
                std::function<std::string(const LibraryIndex::Book&)> customThumbnail,
                std::function<bool()> stripLeadingFolderSlash);

  void reset();
  bool handleInput();
  void render() const;
  int currentPage() const { return page; }
  int totalPages() const { return pageCount(); }

 private:
  // Keep the Statistics proportions while allowing five rows on the normal
  // 800px portrait layout above the bottom navigation menu.
  static constexpr int normalRowHeight = 130;
  static constexpr int sideMargin = 20;
  static constexpr int thumbnailWidth = 74;
  static constexpr int thumbnailHeight = 104;
  static constexpr int textGap = 14;
  static constexpr int caretSize = 30;

  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  const std::vector<LibraryIndex::Book>& items;
  const std::vector<LibraryIndex::Book>& books;
  std::function<void(int, bool)> select;
  std::function<bool(const LibraryIndex::Book&)> isFavorite;
  std::function<void(int, int)> outsideTap;
  std::function<std::string(const LibraryIndex::Book&)> customThumbnail;
  std::function<bool()> stripLeadingFolderSlash;
  int page = 0;

  int top() const;
  int rowHeight() const;
  int visibleRows() const;
  int pageCount() const;
  int itemAt(int x, int y) const;
  static std::string titleFor(const LibraryIndex::Book& book);
  std::string thumbnailPathFor(const LibraryIndex::Book& book) const;
  void renderThumbnail(const LibraryIndex::Book& book, int x, int y) const;
};

}
}

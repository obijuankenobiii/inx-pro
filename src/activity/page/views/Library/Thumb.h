#pragma once

#include <array>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "util/LibraryIndex.h"

class GfxRenderer;
class MappedInputManager;

namespace views {
namespace library {

class Thumb final {
 public:
  Thumb(GfxRenderer& renderer, MappedInputManager& mappedInput,
        const std::vector<LibraryIndex::Book>& items, const std::vector<LibraryIndex::Book>& books,
        std::function<void(int, bool)> select,
        std::function<bool(const LibraryIndex::Book&)> isFavorite,
        std::function<void(int, int)> outsideTap,
        std::function<bool(const LibraryIndex::Book&)> isAuthorFolder);

  static void getThumbnailSize(GfxRenderer& renderer, int& width, int& height);

  void setRoot(bool value);
  void load();
  bool loadNext();
  /** Resolves the NEXT page's covers into resolvedCache_ while idle, so turning to it is a cache hit. */
  bool prefetchNextPage();
  void reset();
  /** Current page index, so the caller can restore it when returning to this folder. */
  int currentPage() const { return page; }
  /** Restores a remembered page. Clamped in load(), which knows the item count. */
  void setPage(int value) { page = value < 0 ? 0 : value; }
  bool handleInput();
  void render() const;

 private:
  struct Thumbnail {
    std::string item;
    std::string first;
    std::string second;
    std::string third;
    std::string singleBookTitle;
    std::string image;
    int imageWidth = 0;
    int imageHeight = 0;
    int bookCount = 0;
    int folderCount = 0;
    bool loaded = false;
  };

  struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
  };

  static constexpr int childItemsPerPage = 4;
  static constexpr int childColumns = 2;
  static constexpr int childRows = 2;
  static constexpr int margin = 20;
  static constexpr int sideMargin = 20;
  static constexpr int gap = 20;
  static constexpr int rowGap = 20;

  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  const std::vector<LibraryIndex::Book>& items;
  const std::vector<LibraryIndex::Book>& books;
  std::vector<Thumbnail> thumbnails;
  std::unordered_map<std::string, Thumbnail> resolvedCache_;
  std::array<Rect, childItemsPerPage> cells;
  std::function<void(int, bool)> select;
  std::function<bool(const LibraryIndex::Book&)> isFavorite;
  std::function<void(int, int)> outsideTap;
  std::function<bool(const LibraryIndex::Book&)> isAuthorFolder;
  bool root = true;
  int page = 0;
  unsigned long loadAt = 0;

  int itemsPerPage() const;
  int pageCount() const;
  int top() const;
  void calculate();
  const Thumbnail* find(const std::string& item) const;
  void drawItem(const LibraryIndex::Book& item, int x, int y, int width, int height, bool favorite,
                bool authorFolder) const;
  void itemBounds(int index, int& x, int& y, int& width, int& height) const;
  int itemAt(int x, int y) const;
};

}
}

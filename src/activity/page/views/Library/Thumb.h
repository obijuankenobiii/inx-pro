#pragma once

#include <array>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ThumbLayout.h"
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
        std::function<std::vector<std::string>(const LibraryIndex::Book&, int)> folderCovers);

  static void getThumbnailSize(GfxRenderer& renderer, int& width, int& height);

  void setLayout(ThumbLayout value) { layout = value; }
  void setRoot(bool value);
  void load();
  bool loadNext();
  /** Resolves the NEXT page's covers into resolvedCache_ while idle, so turning to it is a cache hit. */
  bool prefetchNextPage();
  void reset();
  /** Current page index, so the caller can restore it when returning to this folder. */
  int currentPage() const { return page; }
  int totalPages() const { return pageCount(); }
  /** Restores a remembered page. Clamped in load(), which knows the item count. */
  void setPage(int value) { page = value < 0 ? 0 : value; }
  bool handleInput();
  bool render() const;

 private:
  struct Thumbnail {
    std::string item;
    std::string first;
    std::string singleBookTitle;
    std::string image;
    int imageWidth = 0;
    int imageHeight = 0;
    int bookCount = 0;
    int folderCount = 0;
    int progressPercent = 0;
    bool loaded = false;
    bool pathResolved = false;
  };

  struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
  };

  static constexpr int maxItemsPerPage = 6;
  static constexpr int margin = 20;
  static constexpr int sideMargin = 20;
  static constexpr int gap = 20;
  static constexpr int rowGap = 10;

  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  const std::vector<LibraryIndex::Book>& items;
  const std::vector<LibraryIndex::Book>& books;
  std::vector<Thumbnail> thumbnails;
  std::unordered_map<std::string, Thumbnail> resolvedCache_;
  std::array<Rect, maxItemsPerPage> cells;
  std::function<void(int, bool)> select;
  std::function<bool(const LibraryIndex::Book&)> isFavorite;
  std::function<void(int, int)> outsideTap;
  std::function<std::vector<std::string>(const LibraryIndex::Book&, int)> customFolderCovers;
  bool root = true;
  ThumbLayout layout = standardThumbLayout();
  int page = 0;
  unsigned long loadAt = 0;

  int itemsPerPage() const;
  int columnCount() const { return layout.columns; }
  int rowCount() const { return layout.rows; }
  int pageCount() const;
  int top() const;
  void calculate();
  const Thumbnail* find(const std::string& item) const;
  bool drawItem(const LibraryIndex::Book& item, int x, int y, int width, int height, bool favorite) const;
  void itemBounds(int index, int& x, int& y, int& width, int& height) const;
  int itemAt(int x, int y) const;
  std::string thumbnailPathForBook(const std::string& bookPath);
  std::vector<std::string> coversForFolder(const LibraryIndex::Book& folder, int limit);
  void preloadBookCache(const Thumbnail& thumbnail, int x, int y, int width, int height) const;
  void preloadFolderCache(const Thumbnail& thumbnail, int x, int y, int width, int height) const;
  void preloadImageCache(const std::string& path, int x, int y, int width, int height, bool cropToFill,
                         float cropAnchorX) const;



  std::unordered_map<std::string, std::string> thumbnailPathCache_;
};

}
}

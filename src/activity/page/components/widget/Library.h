#pragma once

#include <array>
#include <string>
#include <vector>

#include "BaseCarousel.h"
#include "util/LibraryIndex.h"

/** Home widget that displays three selected library folders as a vertical stack. */
class LibraryWidget final : public BaseCarousel {
 public:
  static constexpr int kFolderCount = 3;
  static constexpr int kVisibleFolderCount = 2;
  using FolderPaths = std::array<std::string, kFolderCount>;

  struct Folder {
    std::string path;
    std::string name;
  };

  explicit LibraryWidget(GfxRenderer& renderer) : BaseCarousel(renderer) {}

  void render(int x, int y, int width, int height, const char (*folders)[128], bool background = false,
              bool showLabel = true) const;
  void preview(int x, int y, int width, int height, const char (*folders)[128] = nullptr, bool background = false,
               bool showLabel = true) const;
  bool hitTest(int x, int y, int areaX, int areaY, int areaW, int areaH) const;
  void invalidate() const;
  static std::vector<Folder> folders();

 private:
  void load() const;
  std::vector<std::string> covers(const std::string& folder) const;
  int folderBookCount(const std::string& folder) const;
  std::string folderImagePath(const std::string& folder) const;
  std::string folderName(const std::string& folder) const;
  void renderStack(const std::vector<std::string>& paths, int x, int y, int width, int height) const;
  void renderFolder(int x, int y, int width, int height, const char* folder, bool showLabel) const;
  void renderFolders(int x, int y, int width, int height, const char (*folders)[128], bool showLabel) const;

  mutable bool loaded_ = false;
  mutable std::vector<LibraryIndex::Book> books_;
};

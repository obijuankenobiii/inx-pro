#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "EpubBookmark.h"

class Epub;

/** Persistent bookmark collection for one EPUB cache directory. */
class EpubBookmarks {
 public:
  static constexpr size_t kMaxBookmarks = 200;

  enum class ToggleResult { Added, Removed, Full, Unchanged };

  void load(const Epub& epub);
  void load(const std::string& cachePath);
  void save(const Epub& epub) const;
  void save(const std::string& cachePath) const;

  ToggleResult toggle(uint16_t spineIndex, uint16_t pageNumber, uint16_t pageCount, const std::string& chapterTitle,
                      uint32_t timestamp);
  bool remove(const Epub& epub, size_t index);
  bool remove(const std::string& cachePath, size_t index);
  bool contains(uint16_t spineIndex, uint16_t pageNumber) const;
  const EpubBookmark* at(size_t index) const;

  const std::vector<EpubBookmark>& entries() const { return entries_; }

 private:
  static std::string pathFor(const std::string& cachePath);

  std::vector<EpubBookmark> entries_;
};

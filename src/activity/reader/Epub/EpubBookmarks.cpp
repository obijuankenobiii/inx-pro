#include "EpubBookmarks.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstring>

#include "state/EpubNotesIndex.h"

namespace {
constexpr const char* kBookmarksFilename = "bookmarks.bin";
}

std::string EpubBookmarks::pathFor(const std::string& cachePath) { return cachePath + "/" + kBookmarksFilename; }

void EpubBookmarks::load(const Epub& epub) { load(epub.getCachePath()); }

void EpubBookmarks::load(const std::string& cachePath) {
  entries_.clear();
  const std::string path = pathFor(cachePath);
  FsFile file;
  if (!SdMan.openFileForRead("ERS", path, file)) {
    return;
  }

  const uint32_t fileSize = file.fileSize();
  const size_t count = fileSize / sizeof(EpubBookmark);
  if (count > 0 && count <= kMaxBookmarks && fileSize == count * sizeof(EpubBookmark)) {
    entries_.resize(count);
    if (file.read(entries_.data(), fileSize) == fileSize) {
      entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                    [](const EpubBookmark& bookmark) { return !bookmark.isValid(); }),
                     entries_.end());
    } else {
      entries_.clear();
    }
  }
  file.close();
}

void EpubBookmarks::save(const Epub& epub) const { save(epub.getCachePath()); }

void EpubBookmarks::save(const std::string& cachePath) const {
  const std::string path = pathFor(cachePath);
  if (entries_.empty()) {
    SdMan.remove(path.c_str());
    EpubNotesIndex::invalidate();
    return;
  }

  FsFile file;
  if (!SdMan.openFileForWrite("ERS", path, file)) {
    return;
  }
  file.write(entries_.data(), entries_.size() * sizeof(EpubBookmark));
  file.close();
  EpubNotesIndex::invalidate();
}

EpubBookmarks::ToggleResult EpubBookmarks::toggle(const uint16_t spineIndex, const uint16_t pageNumber,
                                                   const uint16_t pageCount, const std::string& chapterTitle,
                                                   const uint32_t timestamp) {
  const auto found = std::find_if(entries_.begin(), entries_.end(), [spineIndex, pageNumber](const EpubBookmark& b) {
    return b.spineIndex == spineIndex && b.pageNumber == pageNumber;
  });
  if (found != entries_.end()) {
    entries_.erase(found);
    return ToggleResult::Removed;
  }
  if (entries_.size() >= kMaxBookmarks) {
    return ToggleResult::Full;
  }

  EpubBookmark bookmark{};
  bookmark.spineIndex = spineIndex;
  bookmark.pageNumber = pageNumber;
  bookmark.pageCount = pageCount;
  bookmark.timestamp = timestamp;
  std::strncpy(bookmark.chapterTitle, chapterTitle.c_str(), sizeof(bookmark.chapterTitle) - 1);
  bookmark.chapterTitle[sizeof(bookmark.chapterTitle) - 1] = '\0';
  entries_.push_back(bookmark);
  return ToggleResult::Added;
}

bool EpubBookmarks::remove(const Epub& epub, const size_t index) { return remove(epub.getCachePath(), index); }

bool EpubBookmarks::remove(const std::string& cachePath, const size_t index) {
  if (index >= entries_.size()) {
    return false;
  }
  entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
  save(cachePath);
  return true;
}

bool EpubBookmarks::contains(const uint16_t spineIndex, const uint16_t pageNumber) const {
  return std::any_of(entries_.begin(), entries_.end(), [spineIndex, pageNumber](const EpubBookmark& bookmark) {
    return bookmark.spineIndex == spineIndex && bookmark.pageNumber == pageNumber;
  });
}

const EpubBookmark* EpubBookmarks::at(const size_t index) const {
  return index < entries_.size() ? &entries_[index] : nullptr;
}

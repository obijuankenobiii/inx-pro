#pragma once

#include <cstdint>

/** One saved reader bookmark, stored directly in each book's bookmarks.bin file. */
struct EpubBookmark {
  uint16_t spineIndex;
  uint16_t pageNumber;
  uint16_t pageCount;
  char chapterTitle[64];
  uint32_t timestamp;

  bool isValid() const { return spineIndex != 0xFFFF && pageNumber != 0xFFFF; }
};

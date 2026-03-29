#pragma once

#include <vector>

#include "util/LibraryIndex.h"

/** Supplies a flat list of every indexed book, without folder entries. */
class AllBooksLibrary final {
 public:
  static bool load(std::vector<LibraryIndex::Book>& books);
};

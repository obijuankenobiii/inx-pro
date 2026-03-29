#pragma once

#include <SDCardManager.h>

#include <functional>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

/** Builds and inspects the on-disk library.idx file. */
class LibraryIndex {
 public:
  static constexpr size_t all = std::numeric_limits<size_t>::max();

  struct Book {
    enum class Type { BOOK, FOLDER };

    Type type = Type::BOOK;
    std::string path;
    std::string title;
    std::string folder;
    // Populated only for the optional Author view; library.idx itself stays unchanged.
    std::string author;
    uint16_t bookCount = 0;
    uint16_t folderCount = 0;
    bool hasMetadata = false;
  };

  static bool hasIndex();
  static bool deleteIndex();
  static int countBooks(FsFile& dir, int depth = 0);
  static void indexAll(const std::function<void(int, int, const char*)>& progress);
  static bool search(const std::string& query, std::vector<Book>& results, size_t limit = 24);

 private:
  static bool openNextEntry(FsFile& dir, FsFile& file);
  static bool shouldSkipEntry(const char* name);
  static void indexDirectory(FsFile& dir, FsFile& index, int& currentBook, int totalBooks,
                             const std::function<void(int, int, const char*)>& progress,
                             const std::string& currentPath, int depth);
  static std::string extractFolderName(const char* path);
  static std::string cleanFilename(const char* name);
};

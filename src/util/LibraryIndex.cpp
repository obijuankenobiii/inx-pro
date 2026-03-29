#include "LibraryIndex.h"

#include <Arduino.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <strings.h>

#include "SdIoMutex.h"

namespace {

constexpr char indexPath[] = "/.metadata/library/library.idx";
constexpr char searchPath[] = "/.metadata/library/search";
constexpr char searchMagic[] = "LIXS";
constexpr uint8_t indexVersion = 2;
constexpr uint8_t legacyIndexVersion = 1;
constexpr uint8_t searchVersion = 1;
constexpr int bucketCount = 26;

bool readText(FsFile& file, const size_t length, std::string& value) {
  value.clear();
  if (length == 0) return true;

  std::vector<char> buffer(length + 1, '\0');
  if (file.read(buffer.data(), length) != length) return false;
  value.assign(buffer.data(), length);
  return true;
}

bool skipText(FsFile& file, const size_t length) {
  return file.seek(file.position() + length);
}

std::string lower(const std::string& value) {
  std::string out = value;
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

int matchScore(const std::string& query, const LibraryIndex::Book& book) {
  if (query.empty()) return 0;

  const std::string needle = lower(query);
  const std::string title = lower(book.title);
  const std::string folder = lower(book.folder);
  const std::string path = lower(book.path);

  if (title == needle) return 0;
  if (title.rfind(needle, 0) == 0) return 10;
  if (title.find(needle) != std::string::npos) return 20;
  if (folder.find(needle) != std::string::npos) return 40;
  if (path.find(needle) != std::string::npos) return 60;

  // Basic fuzzy matching: all query characters must occur in order. Lower gap
  // count ranks the result higher without requiring a heavyweight search index.
  size_t at = 0;
  int gaps = 0;
  for (const char c : needle) {
    const size_t found = title.find(c, at);
    if (found == std::string::npos) return -1;
    gaps += static_cast<int>(found - at);
    at = found + 1;
  }
  return 100 + gaps;
}

bool readBook(FsFile& file, LibraryIndex::Book& book) {
  uint16_t pathLength = 0;
  if (file.read(&pathLength, sizeof(pathLength)) != sizeof(pathLength)) return false;
  if (!readText(file, pathLength, book.path)) return false;

  uint8_t nameLength = 0;
  if (file.read(&nameLength, sizeof(nameLength)) != sizeof(nameLength)) return false;
  if (!skipText(file, nameLength)) return false;

  uint8_t titleLength = 0;
  if (file.read(&titleLength, sizeof(titleLength)) != sizeof(titleLength)) return false;
  if (!readText(file, titleLength, book.title)) return false;

  uint8_t folderLength = 0;
  if (file.read(&folderLength, sizeof(folderLength)) != sizeof(folderLength)) return false;
  return readText(file, folderLength, book.folder);
}

bool readDirectory(FsFile& file, const uint8_t version, std::string& path, uint16_t& entryCount,
                   uint16_t& bookCount, uint16_t& folderCount) {
  uint16_t pathLength = 0;
  if (file.read(&pathLength, sizeof(pathLength)) != sizeof(pathLength) || !readText(file, pathLength, path)) {
    return false;
  }

  entryCount = 0;
  bookCount = 0;
  folderCount = 0;
  if (file.read(&entryCount, sizeof(entryCount)) != sizeof(entryCount)) return false;
  if (version == legacyIndexVersion) return true;
  return file.read(&bookCount, sizeof(bookCount)) == sizeof(bookCount) &&
         file.read(&folderCount, sizeof(folderCount)) == sizeof(folderCount);
}

std::string folderName(const std::string& path) {
  if (path.empty() || path == "/") return "Root";
  const size_t slash = path.find_last_of('/');
  const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  return name.empty() ? "Root" : name;
}

bool readEntry(FsFile& file, const uint8_t marker, const uint8_t version, LibraryIndex::Book& book, bool& found) {
  found = false;
  if (marker == 0x01) {
    book = {};
    found = readBook(file, book);
    return found;
  }
  if (marker == 0xFF) {
    std::string path;
    uint16_t entryCount = 0;
    uint16_t bookCount = 0;
    uint16_t folderCount = 0;
    if (!readDirectory(file, version, path, entryCount, bookCount, folderCount)) return false;
    if (path == "/" || entryCount == 0) return true;
    book = {};
    book.type = LibraryIndex::Book::Type::FOLDER;
    book.path = path;
    book.title = folderName(path);
    const size_t slash = path.find_last_of('/');
    book.folder = slash <= 0 ? "Library" : path.substr(0, slash);
    book.bookCount = bookCount;
    book.folderCount = folderCount;
    book.hasMetadata = version == indexVersion;
    found = true;
    return true;
  }
  return marker == 0xFE;
}

bool readIndexHeader(FsFile& file, uint8_t& version) {
  char magic[4] = {};
  return file.read(magic, sizeof(magic)) == sizeof(magic) && memcmp(magic, "LIBX", sizeof(magic)) == 0 &&
         file.read(&version, sizeof(version)) == sizeof(version) &&
         (version == legacyIndexVersion || version == indexVersion);
}

int bucket(const char value) {
  const char letter = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  return letter >= 'a' && letter <= 'z' ? letter - 'a' : -1;
}

std::string bucketPath(const int index, const bool temporary = false) {
  std::string path = std::string(searchPath) + "/" + static_cast<char>('a' + index) + ".idx";
  if (temporary) path += ".tmp";
  return path;
}

void removeSearchIndex() {
  for (int index = 0; index < bucketCount; ++index) {
    const std::string path = bucketPath(index);
    const std::string temporary = bucketPath(index, true);
    SdMan.remove(path.c_str());
    SdMan.remove(temporary.c_str());
  }
}

void addOffset(const LibraryIndex::Book& book, const uint32_t offset,
               std::array<std::vector<uint32_t>, bucketCount>& offsets) {
  std::array<bool, bucketCount> used{};
  const auto addText = [&used, &offsets](const std::string& text) {
    for (const char value : text) {
      const int index = bucket(value);
      if (index >= 0) used[static_cast<size_t>(index)] = true;
    }
  };
  addText(book.title);
  addText(book.folder);
  addText(book.path);
  for (int index = 0; index < bucketCount; ++index) {
    if (used[static_cast<size_t>(index)]) offsets[static_cast<size_t>(index)].push_back(offset);
  }
}

bool writeSearchIndex(const std::array<std::vector<uint32_t>, bucketCount>& offsets, const uint32_t sourceSize) {
  SdMan.mkdir(searchPath);
  for (int index = 0; index < bucketCount; ++index) {
    const std::string temporary = bucketPath(index, true);
    const std::string path = bucketPath(index);
    SdMan.remove(temporary.c_str());
    FsFile file = SdMan.open(temporary.c_str(), O_WRITE | O_CREAT | O_TRUNC);
    if (!file) return false;

    const uint32_t count = static_cast<uint32_t>(offsets[static_cast<size_t>(index)].size());
    bool written = file.write(searchMagic, 4) == 4 && file.write(&searchVersion, sizeof(searchVersion)) == sizeof(searchVersion) &&
                   file.write(&sourceSize, sizeof(sourceSize)) == sizeof(sourceSize) &&
                   file.write(&count, sizeof(count)) == sizeof(count);
    if (written && count > 0) {
      const size_t bytes = static_cast<size_t>(count) * sizeof(uint32_t);
      written = file.write(offsets[static_cast<size_t>(index)].data(), bytes) == bytes;
    }
    file.close();
    if (!written) {
      SdMan.remove(temporary.c_str());
      return false;
    }
    SdMan.remove(path.c_str());
    if (!SdMan.rename(temporary.c_str(), path.c_str())) {
      SdMan.remove(temporary.c_str());
      return false;
    }
  }
  return true;
}

bool buildSearchIndex() {
  FsFile file = SdMan.open(indexPath, O_READ);
  uint8_t version = 0;
  if (!file || !readIndexHeader(file, version)) {
    if (file) file.close();
    return false;
  }

  const uint32_t sourceSize = static_cast<uint32_t>(file.size());
  std::array<std::vector<uint32_t>, bucketCount> offsets;
  int entries = 0;
  while (file.available()) {
    const uint32_t offset = file.position();
    uint8_t marker = 0;
    if (file.read(&marker, sizeof(marker)) != sizeof(marker)) break;
    LibraryIndex::Book book;
    bool found = false;
    if (!readEntry(file, marker, version, book, found)) {
      file.close();
      return false;
    }
    if (found) addOffset(book, offset, offsets);
    if (++entries % 64 == 0) vTaskDelay(pdMS_TO_TICKS(1));
  }
  file.close();
  return writeSearchIndex(offsets, sourceSize);
}

bool readSearchOffsets(const int index, const uint32_t sourceSize, std::vector<uint32_t>& offsets) {
  offsets.clear();
  if (index < 0 || index >= bucketCount) return false;
  FsFile file = SdMan.open(bucketPath(index).c_str(), O_READ);
  if (!file) return false;

  char magic[4] = {};
  uint8_t version = 0;
  uint32_t indexedSize = 0;
  uint32_t count = 0;
  bool valid = file.read(magic, sizeof(magic)) == sizeof(magic) && memcmp(magic, searchMagic, sizeof(magic)) == 0 &&
               file.read(&version, sizeof(version)) == sizeof(version) && version == searchVersion &&
               file.read(&indexedSize, sizeof(indexedSize)) == sizeof(indexedSize) && indexedSize == sourceSize &&
               file.read(&count, sizeof(count)) == sizeof(count);
  const uint32_t remaining = valid && file.size() >= file.position()
                                 ? static_cast<uint32_t>(file.size() - file.position())
                                 : 0;
  valid = valid && count <= remaining / sizeof(uint32_t);
  if (valid && count > 0) {
    offsets.resize(count);
    valid = file.read(offsets.data(), static_cast<size_t>(count) * sizeof(uint32_t)) ==
            static_cast<size_t>(count) * sizeof(uint32_t);
  }
  file.close();
  if (!valid) offsets.clear();
  return valid;
}

struct Match {
  int score;
  LibraryIndex::Book book;
};

void addMatch(const std::string& query, LibraryIndex::Book&& book, std::vector<Match>& matches) {
  const int score = matchScore(query, book);
  if (score >= 0) matches.push_back({score, std::move(book)});
}

bool scanIndex(FsFile& file, const uint8_t version, const std::string& query, std::vector<Match>& matches) {
  while (file.available()) {
    uint8_t marker = 0;
    if (file.read(&marker, sizeof(marker)) != sizeof(marker)) return false;
    LibraryIndex::Book book;
    bool found = false;
    if (!readEntry(file, marker, version, book, found)) return false;
    if (found) addMatch(query, std::move(book), matches);
  }
  return true;
}

}  // namespace

bool LibraryIndex::hasIndex() { return SdMan.exists(indexPath); }

bool LibraryIndex::deleteIndex() {
  removeSearchIndex();
  return SdMan.remove(indexPath);
}

int LibraryIndex::countBooks(FsFile& dir, const int depth) {
  if (depth > 32) {
    return 0;
  }

  int count = 0;
  dir.rewindDirectory();
  char name[256];
  FsFile file;
  while (openNextEntry(dir, file)) {
    file.getName(name, sizeof(name));
    if (shouldSkipEntry(name)) {
      file.close();
      continue;
    }

    if (file.isDirectory()) {
      count += countBooks(file, depth + 1);
    } else {
      const char* extension = strrchr(name, '.');
      if (extension && (strcasecmp(extension, ".epub") == 0 || strcasecmp(extension, ".txt") == 0 ||
                        strcasecmp(extension, ".md") == 0 || strcasecmp(extension, ".xtc") == 0 ||
                        strcasecmp(extension, ".xtch") == 0 || strcasecmp(extension, ".pdf") == 0)) {
        ++count;
      }
    }
    file.close();
  }
  return count;
}

void LibraryIndex::indexAll(const std::function<void(int, int, const char*)>& progress) {
  SdIoMutex::Lock lock;
  if (!SdMan.exists("/.metadata")) {
    SdMan.mkdir("/.metadata");
  }
  if (!SdMan.exists("/.metadata/library")) {
    SdMan.mkdir("/.metadata/library");
  }
  // Start from a clean catalog. Some SD/FAT combinations can leave the previous index contents
  // visible when a file is reopened with O_TRUNC after an interrupted rebuild; deleting it first
  // matches the manual recovery path and prevents stale entries from surviving this rebuild.
  SdMan.remove(indexPath);
  removeSearchIndex();

  vTaskDelay(10 / portTICK_PERIOD_MS);

  int totalBooks = 0;
  FsFile root = SdMan.open("/");
  if (root) {
    totalBooks = countBooks(root);
    root.close();
  }

  if (totalBooks == 0) {
    if (progress) {
      progress(0, 0, "No books found");
    }
    return;
  }

  if (progress) {
    progress(0, totalBooks, "");
  }

  FsFile index = SdMan.open(indexPath, O_WRITE | O_CREAT | O_TRUNC);
  if (!index) {
    if (progress) {
      progress(0, 0, "Failed to create index");
    }
    return;
  }

  index.write("LIBX", 4);
  uint8_t version = indexVersion;
  index.write(&version, 1);

  root = SdMan.open("/");
  if (root) {
    int currentBook = 0;
    indexDirectory(root, index, currentBook, totalBooks, progress, std::string("/"), 0);
    root.close();
  }

  index.close();
  if (!buildSearchIndex()) {
    INX_SERIAL.printf("[LIBRARY] Search index unavailable; using library.idx scan\n");
  }
  if (progress) {
    progress(totalBooks, totalBooks, "Indexing complete!");
  }
}

bool LibraryIndex::search(const std::string& query, std::vector<Book>& results, const size_t limit) {
  SdIoMutex::Lock lock;
  results.clear();
  if (!hasIndex() || limit == 0) return false;

  FsFile file = SdMan.open(indexPath, O_READ);
  if (!file) return false;
  uint8_t version = 0;
  if (!readIndexHeader(file, version)) {
    file.close();
    return false;
  }

  // The library screen asks for every entry with an empty query. Avoid building a second full
  // Match vector in that case: on large libraries the temporary vector used to nearly double the
  // catalog's peak heap usage before its Books were moved into results.
  if (query.empty()) {
    file.seek(5);
    while (file.available()) {
      uint8_t marker = 0;
      if (file.read(&marker, sizeof(marker)) != sizeof(marker)) {
        file.close();
        results.clear();
        return false;
      }
      Book book;
      bool found = false;
      if (!readEntry(file, marker, version, book, found)) {
        file.close();
        results.clear();
        return false;
      }
      if (found) {
        results.push_back(std::move(book));
      }
    }
    file.close();

    std::sort(results.begin(), results.end(), [](const Book& left, const Book& right) {
      return lower(left.title) < lower(right.title);
    });
    if (results.size() > limit) {
      results.resize(limit);
    }
    return true;
  }

  std::vector<Match> matches;
  bool searched = false;
  const int index = query.empty() ? -1 : bucket(query.front());
  std::vector<uint32_t> offsets;
  if (index >= 0 && readSearchOffsets(index, static_cast<uint32_t>(file.size()), offsets)) {
    searched = true;
    for (const uint32_t offset : offsets) {
      if (!file.seek(offset)) {
        searched = false;
        break;
      }
      uint8_t marker = 0;
      if (file.read(&marker, sizeof(marker)) != sizeof(marker)) {
        searched = false;
        break;
      }
      Book book;
      bool found = false;
      if (!readEntry(file, marker, version, book, found) || !found) {
        searched = false;
        break;
      }
      addMatch(query, std::move(book), matches);
    }
  }
  if (!searched) {
    matches.clear();
    file.seek(5);
    if (!scanIndex(file, version, query, matches)) {
      file.close();
      return false;
    }
  }
  file.close();

  std::sort(matches.begin(), matches.end(), [](const Match& left, const Match& right) {
    if (left.score != right.score) return left.score < right.score;
    return lower(left.book.title) < lower(right.book.title);
  });

  const size_t count = std::min(limit, matches.size());
  results.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    results.push_back(std::move(matches[i].book));
  }
  return true;
}

bool LibraryIndex::openNextEntry(FsFile& dir, FsFile& file) {
#ifdef SIMULATOR
  file = dir.openNextFile();
  return static_cast<bool>(file);
#else
  return file.openNext(&dir, O_RDONLY);
#endif
}

bool LibraryIndex::shouldSkipEntry(const char* name) {
  return name == nullptr || name[0] == '.' || strcmp(name, ".metadata") == 0 || strcasecmp(name, "sleep") == 0 ||
         strcasecmp(name, "fonts") == 0 || strcasecmp(name, "dictionaries") == 0;
}

void LibraryIndex::indexDirectory(FsFile& dir, FsFile& index, int& currentBook, const int totalBooks,
                                  const std::function<void(int, int, const char*)>& progress,
                                  const std::string& currentPath, const int depth) {
  if (depth > 32) {
    return;
  }

  dir.rewindDirectory();
  char name[256];

  uint8_t directoryMarker = 0xFF;
  index.write(&directoryMarker, 1);
  const uint16_t pathLength = static_cast<uint16_t>(currentPath.length());
  index.write(&pathLength, sizeof(pathLength));
  index.write(currentPath.c_str(), pathLength);

  uint16_t entryCount = 0;
  uint16_t bookCount = 0;
  uint16_t folderCount = 0;
  const uint32_t countPosition = index.position();
  index.write(&entryCount, sizeof(entryCount));
  const uint32_t bookCountPosition = index.position();
  index.write(&bookCount, sizeof(bookCount));
  const uint32_t folderCountPosition = index.position();
  index.write(&folderCount, sizeof(folderCount));

  FsFile file;
  while (openNextEntry(dir, file)) {
    file.getName(name, sizeof(name));
    if (shouldSkipEntry(name)) {
      file.close();
      continue;
    }

    const std::string itemPath = currentPath == "/" ? "/" + std::string(name) : currentPath + "/" + name;
    if (file.isDirectory()) {
      indexDirectory(file, index, currentBook, totalBooks, progress, itemPath, depth + 1);
      ++entryCount;
      ++folderCount;
    } else {
      const char* extension = strrchr(name, '.');
      if (extension && (strcasecmp(extension, ".epub") == 0 || strcasecmp(extension, ".txt") == 0 ||
                        strcasecmp(extension, ".md") == 0 || strcasecmp(extension, ".xtc") == 0 ||
                        strcasecmp(extension, ".xtch") == 0 || strcasecmp(extension, ".pdf") == 0)) {
        uint8_t bookMarker = 0x01;
        index.write(&bookMarker, 1);

        const uint16_t itemPathLength = static_cast<uint16_t>(itemPath.length());
        index.write(&itemPathLength, sizeof(itemPathLength));
        index.write(itemPath.c_str(), itemPathLength);

        const uint8_t nameLength = static_cast<uint8_t>(strlen(name));
        index.write(&nameLength, sizeof(nameLength));
        index.write(name, nameLength);

        const std::string displayName = cleanFilename(name);
        const uint8_t displayNameLength = static_cast<uint8_t>(displayName.length());
        index.write(&displayNameLength, sizeof(displayNameLength));
        index.write(displayName.c_str(), displayNameLength);

        const std::string folderName = extractFolderName(currentPath.c_str());
        const uint8_t folderNameLength = static_cast<uint8_t>(folderName.length());
        index.write(&folderNameLength, sizeof(folderNameLength));
        index.write(folderName.c_str(), folderNameLength);

        ++currentBook;
        ++entryCount;
        ++bookCount;
        if (progress) {
          progress(currentBook, totalBooks, name);
        }
      }
    }
    file.close();
  }

  const uint32_t endPosition = index.position();
  index.seek(countPosition);
  index.write(&entryCount, sizeof(entryCount));
  index.seek(bookCountPosition);
  index.write(&bookCount, sizeof(bookCount));
  index.seek(folderCountPosition);
  index.write(&folderCount, sizeof(folderCount));
  index.seek(endPosition);

  uint8_t endMarker = 0xFE;
  index.write(&endMarker, 1);
}

std::string LibraryIndex::extractFolderName(const char* path) {
  std::string result = path ? path : "";
  if (result.empty() || result == "/") {
    return "Root";
  }
  if (result.back() == '/') {
    result.pop_back();
  }
  const size_t slash = result.find_last_of('/');
  if (slash != std::string::npos) {
    const std::string folder = result.substr(slash + 1);
    return folder.empty() ? "Root" : folder;
  }
  return result;
}

std::string LibraryIndex::cleanFilename(const char* name) {
  std::string result = name ? name : "";
  const size_t dot = result.find_last_of('.');
  if (dot != std::string::npos) {
    result.resize(dot);
  }
  std::replace(result.begin(), result.end(), '_', ' ');
  std::replace(result.begin(), result.end(), '-', ' ');
  return result;
}

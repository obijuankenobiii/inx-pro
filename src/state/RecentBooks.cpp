/**
 * @file RecentBooks.cpp
 * @brief Definitions for RecentBooks.
 */

#include "state/RecentBooks.h"

#include <Epub/BookMetadataCache.h>
#include <HardwareSerial.h>
#include <ImageRender.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <utility>

#include "util/SdIoMutex.h"

namespace {
constexpr uint8_t RECENT_BOOKS_FILE_VERSION = 5;
constexpr char RECENT_BOOKS_FILE[] = "/.metadata/recent.bin";
constexpr int MAX_RECENT_BOOKS = 10;
constexpr size_t MAX_RECENT_DESCRIPTION_BYTES = 768;

std::string cachePathFor(const RecentBook& book) {
  if (!book.cachePath.empty()) return book.cachePath;
  if (book.path.empty()) return {};
  return "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(book.path));
}

std::string plainDescription(const std::string& raw) {
  std::string output;
  output.reserve(std::min(raw.size(), MAX_RECENT_DESCRIPTION_BYTES));
  bool inTag = false;
  bool previousSpace = false;
  for (const char character : raw) {
    if (character == '<') {
      inTag = true;
      continue;
    }
    if (character == '>') {
      inTag = false;
      continue;
    }
    if (inTag) continue;
    const bool space = std::isspace(static_cast<unsigned char>(character)) != 0;
    if (space) {
      if (!previousSpace) output.push_back(' ');
    } else {
      output.push_back(character);
    }
    previousSpace = space;
    if (output.size() >= MAX_RECENT_DESCRIPTION_BYTES) break;
  }
  while (!output.empty() && output.back() == ' ') output.pop_back();
  return output;
}

void hydrateUiMetadata(RecentBook& book, const bool forceRefresh) {
  if (book.path.empty() || (!forceRefresh && book.uiMetadataReady != 0)) return;

  if (book.cachePath.empty()) book.cachePath = cachePathFor(book);

  if (book.thumbnailPath.empty() && !book.cachePath.empty()) {
    SdIoMutex::Lock ioLock;
    char path[192] = {};
    for (const char* extension : {"thumb.jpg", "thumb.png", "thumb.bmp"}) {
      std::snprintf(path, sizeof(path), "%s/%s", book.cachePath.c_str(), extension);
      if (SdMan.exists(path)) {
        book.thumbnailPath = path;
        break;
      }
    }
  }

  if (!book.thumbnailPath.empty() && (book.thumbnailWidth == 0 || book.thumbnailHeight == 0)) {
    int width = 0;
    int height = 0;
    if (ImageRender::getDimensions(book.thumbnailPath, &width, &height) && width > 0 && height > 0) {
      book.thumbnailWidth = static_cast<uint16_t>(std::min(width, 65535));
      book.thumbnailHeight = static_cast<uint16_t>(std::min(height, 65535));
    }
  }

  if (!book.cachePath.empty() &&
      (book.title.empty() || book.author.empty() || book.description.empty() || book.rating.empty())) {
    BookMetadataCache metadata(book.cachePath);
    if (metadata.load()) {
      if (book.title.empty()) book.title = metadata.coreMetadata.title;
      if (book.author.empty()) book.author = metadata.coreMetadata.author;
      if (book.description.empty()) book.description = plainDescription(metadata.coreMetadata.description);
      if (book.rating.empty()) book.rating = metadata.coreMetadata.rating;
    }
  }

  book.uiMetadataReady = 1;
}
}  // namespace

RecentBooks RecentBooks::instance;

void RecentBooks::addBook(const std::string& path, const std::string& cachePath, const std::string& title,
                          const std::string& author, float progress, const bool saveNow) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });

  if (it != recentBooks.end()) {
    if (!cachePath.empty() && it->cachePath != cachePath) {
      it->cachePath = cachePath;
      it->thumbnailPath.clear();
      it->thumbnailWidth = 0;
      it->thumbnailHeight = 0;
      it->uiMetadataReady = 0;
    }
    if (!title.empty()) it->title = title;
    if (!author.empty()) it->author = author;
    if (progress >= 0.0f) {
      it->progress = progress;
    }
    hydrateUiMetadata(*it, true);
    RecentBook book = *it;
    recentBooks.erase(it);
    recentBooks.insert(recentBooks.begin(), book);
  } else {
    RecentBook book(path, cachePath, title, author, progress);
    hydrateUiMetadata(book, true);
    recentBooks.insert(recentBooks.begin(), std::move(book));
  }

  if (recentBooks.size() > MAX_RECENT_BOOKS) {
    recentBooks.resize(MAX_RECENT_BOOKS);
  }

  if (saveNow) {
    saveToFile();
  }
}

void RecentBooks::updateProgress(const std::string& path, float progress) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    it->progress = progress;
    saveToFile();
  }
}

bool RecentBooks::saveToFile() const {
  SdMan.mkdir("/.metadata");

  FsFile outputFile;
  if (!SdMan.openFileForWrite("RBS", RECENT_BOOKS_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, RECENT_BOOKS_FILE_VERSION);

  uint8_t count = static_cast<uint8_t>(recentBooks.size());
  serialization::writePod(outputFile, count);

  for (const auto& book : recentBooks) {
    serialization::writeString(outputFile, book.path);
    serialization::writeString(outputFile, book.cachePath);
    serialization::writeString(outputFile, book.title);
    serialization::writeString(outputFile, book.author);
    serialization::writePod(outputFile, book.progress);
    serialization::writeString(outputFile, book.thumbnailPath);
    serialization::writeString(outputFile, book.description);
    serialization::writeString(outputFile, book.rating);
    serialization::writePod(outputFile, book.thumbnailWidth);
    serialization::writePod(outputFile, book.thumbnailHeight);
    serialization::writePod(outputFile, book.uiMetadataReady);
  }

  outputFile.close();
  return true;
}

bool RecentBooks::loadFromFile() {
  FsFile inputFile;
  if (!SdMan.openFileForRead("RBS", RECENT_BOOKS_FILE, inputFile)) {
    saveToFile();
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);

  if (version < 1 || version > RECENT_BOOKS_FILE_VERSION) {
    inputFile.close();
    return false;
  }

  if (version == 1 || version == 2) {
    uint8_t count;
    serialization::readPod(inputFile, count);

    if (count > MAX_RECENT_BOOKS * 2) {
      inputFile.close();
      return false;
    }

    recentBooks.clear();
    recentBooks.reserve(count);

    for (uint8_t i = 0; i < count; i++) {
      std::string path, title, author;
      serialization::readString(inputFile, path);
      serialization::readString(inputFile, title);
      serialization::readString(inputFile, author);

      if (path.empty()) {
        recentBooks.clear();
        inputFile.close();
        return false;
      }

      recentBooks.push_back({path, "", title, author, -1.0f});
    }

    for (RecentBook& book : recentBooks) hydrateUiMetadata(book, true);
    saveToFile();
  } else if (version == 3) {
    uint8_t count;
    serialization::readPod(inputFile, count);

    if (count > MAX_RECENT_BOOKS * 2) {
      inputFile.close();
      return false;
    }

    recentBooks.clear();
    recentBooks.reserve(count);

    for (uint8_t i = 0; i < count; i++) {
      std::string path, title, author;
      float progress;
      serialization::readString(inputFile, path);
      serialization::readString(inputFile, title);
      serialization::readString(inputFile, author);
      serialization::readPod(inputFile, progress);

      if (path.empty()) {
        recentBooks.clear();
        inputFile.close();
        return false;
      }

      recentBooks.push_back({path, "", title, author, progress});
    }

    for (RecentBook& book : recentBooks) hydrateUiMetadata(book, true);
    saveToFile();
  } else if (version == 4) {
    uint8_t count;
    serialization::readPod(inputFile, count);

    if (count > MAX_RECENT_BOOKS * 2) {
      inputFile.close();
      return false;
    }

    recentBooks.clear();
    recentBooks.reserve(count);

    for (uint8_t i = 0; i < count; i++) {
      std::string path, cachePath, title, author;
      float progress;
      serialization::readString(inputFile, path);
      serialization::readString(inputFile, cachePath);
      serialization::readString(inputFile, title);
      serialization::readString(inputFile, author);
      serialization::readPod(inputFile, progress);

      if (path.empty()) {
        recentBooks.clear();
        inputFile.close();
        return false;
      }

      recentBooks.push_back({path, cachePath, title, author, progress});
    }
    for (RecentBook& book : recentBooks) hydrateUiMetadata(book, true);
    saveToFile();
  } else {
    uint8_t count;
    serialization::readPod(inputFile, count);

    if (count > MAX_RECENT_BOOKS * 2) {
      inputFile.close();
      return false;
    }

    recentBooks.clear();
    recentBooks.reserve(count);

    for (uint8_t i = 0; i < count; i++) {
      RecentBook book;
      serialization::readString(inputFile, book.path);
      serialization::readString(inputFile, book.cachePath);
      serialization::readString(inputFile, book.title);
      serialization::readString(inputFile, book.author);
      serialization::readPod(inputFile, book.progress);
      serialization::readString(inputFile, book.thumbnailPath);
      serialization::readString(inputFile, book.description);
      serialization::readString(inputFile, book.rating);
      serialization::readPod(inputFile, book.thumbnailWidth);
      serialization::readPod(inputFile, book.thumbnailHeight);
      serialization::readPod(inputFile, book.uiMetadataReady);

      if (book.path.empty()) {
        recentBooks.clear();
        inputFile.close();
        return false;
      }

      recentBooks.push_back(std::move(book));
    }
  }

  inputFile.close();
  return true;
}

void RecentBooks::removeBook(const std::string& path) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });

  if (it != recentBooks.end()) {
    recentBooks.erase(it);
    saveToFile();
  }
}

void RecentBooks::renamePath(const std::string& oldPath, const std::string& newPath, const std::string& newCachePath) {
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == oldPath; });

  if (it != recentBooks.end()) {
    it->path = newPath;
    it->cachePath = newCachePath;
    it->thumbnailPath.clear();
    it->thumbnailWidth = 0;
    it->thumbnailHeight = 0;
    it->uiMetadataReady = 0;
    hydrateUiMetadata(*it, true);
    saveToFile();
  }
}

void RecentBooks::clear(const bool saveNow) {
  recentBooks.clear();
  if (saveNow) {
    saveToFile();
  }
}

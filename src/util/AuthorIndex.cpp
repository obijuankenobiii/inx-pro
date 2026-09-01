#include "AuthorIndex.h"

#include <Arduino.h>
#include <Epub.h>
#include <SDCardManager.h>
#include <Xtc.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>

#include "util/LibraryIndex.h"

namespace {
constexpr char kIndexPath[] = "/.metadata/library/authors.idx";
constexpr char kTemporaryPath[] = "/.metadata/library/authors.idx.tmp";
constexpr char kMagic[] = "AUTH";
constexpr uint8_t kVersion = 1;

bool isSupportedBook(const char* name) {
  if (!name) return false;
  std::string value(name);
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  constexpr const char* extensions[] = {".epub", ".txt", ".md", ".xtc", ".xtch", ".pdf"};
  for (const char* extension : extensions) {
    const size_t length = strlen(extension);
    if (value.size() >= length && value.compare(value.size() - length, length, extension) == 0) return true;
  }
  return false;
}

bool shouldSkip(const char* name) {
  return !name || name[0] == '.' || strcmp(name, ".metadata") == 0 ||
         strcasecmp(name, "sleep") == 0 || strcasecmp(name, "fonts") == 0 ||
         strcasecmp(name, "dictionaries") == 0;
}

bool writeString(FsFile& file, const std::string& value) {
  if (value.size() > UINT16_MAX) return false;
  const uint16_t length = static_cast<uint16_t>(value.size());
  return file.write(&length, sizeof(length)) == sizeof(length) &&
         (length == 0 || file.write(value.data(), length) == length);
}

bool readString(FsFile& file, std::string& value) {
  uint16_t length = 0;
  if (file.read(&length, sizeof(length)) != sizeof(length)) return false;
  const int available = file.available();
  if (available < 0 || length > static_cast<uint16_t>(available)) return false;
  value.clear();
  if (length == 0) return true;
  value.resize(length);
  return file.read(&value[0], length) == length;
}

std::string authorForBook(const std::string& path) {
  std::string lowerPath = path;
  std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  if (lowerPath.size() >= 5 && lowerPath.compare(lowerPath.size() - 5, 5, ".epub") == 0) {
    Epub epub(path, "/.metadata/epub");
    if (!epub.load(false) && !epub.load(true)) return {};
    return epub.getAuthor();
  }
  if ((lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".xtc") == 0) ||
      (lowerPath.size() >= 5 && lowerPath.compare(lowerPath.size() - 5, 5, ".xtch") == 0)) {
    Xtc xtc(path, "/.metadata/xtc");
    return xtc.load() ? xtc.getAuthor() : std::string();
  }
  return {};
}

bool scanPath(const std::string& path, FsFile& output, int& current, const int total, uint32_t& recordCount,
              const std::function<void(int, int, const char*)>& progress,
              const std::function<bool()>& shouldCancel) {
  if (shouldCancel && shouldCancel()) return false;
  FsFile directory = SdMan.open(path.c_str());
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    return true;
  }

  char name[256] = {};
  while (!(shouldCancel && shouldCancel())) {
    FsFile entry = directory.openNextFile();
    if (!entry) break;
    entry.getName(name, sizeof(name));
    if (shouldSkip(name)) {
      entry.close();
      continue;
    }

    const std::string fullPath = path == "/" ? "/" + std::string(name) : path + "/" + name;
    if (entry.isDirectory()) {
      entry.close();
      if (!scanPath(fullPath, output, current, total, recordCount, progress, shouldCancel)) {
        directory.close();
        return false;
      }
      continue;
    }

    const bool supported = isSupportedBook(name);
    entry.close();
    if (!supported) continue;

    const std::string author = authorForBook(fullPath);
    if (!writeString(output, fullPath) || !writeString(output, author)) {
      directory.close();
      return false;
    }
    ++recordCount;
    ++current;
    if (progress) progress(current, total, name);
    if ((current % 4) == 0) vTaskDelay(pdMS_TO_TICKS(1));
  }

  directory.close();
  return !(shouldCancel && shouldCancel());
}
}

bool AuthorIndex::hasIndex() { return SdMan.exists(kIndexPath); }

bool AuthorIndex::load(std::vector<Entry>& entries) {
  entries.clear();
  FsFile file = SdMan.open(kIndexPath, O_READ);
  if (!file) return false;

  char magic[sizeof(kMagic) - 1] = {};
  uint8_t version = 0;
  uint32_t count = 0;
  bool valid = file.read(magic, sizeof(magic)) == sizeof(magic) && memcmp(magic, kMagic, sizeof(magic)) == 0 &&
               file.read(&version, sizeof(version)) == sizeof(version) && version == kVersion &&
               file.read(&count, sizeof(count)) == sizeof(count);
  if (!valid || count > 100000) {
    file.close();
    return false;
  }

  entries.reserve(std::min<uint32_t>(count, 4096));
  for (uint32_t i = 0; i < count; ++i) {
    Entry entry;
    if (!readString(file, entry.path) || !readString(file, entry.author)) {
      entries.clear();
      file.close();
      return false;
    }
    entries.push_back(std::move(entry));
  }
  file.close();
  return true;
}

bool AuthorIndex::generate(const std::function<void(int, int, const char*)>& progress,
                           const std::function<bool()>& shouldCancel) {
  if (!SdMan.exists("/.metadata")) SdMan.mkdir("/.metadata");
  if (!SdMan.exists("/.metadata/library")) SdMan.mkdir("/.metadata/library");

  int total = 0;
  FsFile root = SdMan.open("/");
  if (root) {
    total = LibraryIndex::countBooks(root);
    root.close();
  }
  if (progress) progress(0, total, "");
  if (shouldCancel && shouldCancel()) return false;

  SdMan.remove(kTemporaryPath);
  FsFile output = SdMan.open(kTemporaryPath, O_WRITE | O_CREAT | O_TRUNC);
  if (!output) return false;
  output.write(kMagic, sizeof(kMagic) - 1);
  output.write(&kVersion, sizeof(kVersion));
  const uint32_t countPosition = output.position();
  uint32_t recordCount = 0;
  output.write(&recordCount, sizeof(recordCount));

  int current = 0;
  const bool completed = scanPath("/", output, current, total, recordCount, progress, shouldCancel);
  if (!completed) {
    output.close();
    SdMan.remove(kTemporaryPath);
    return false;
  }

  output.seek(countPosition);
  output.write(&recordCount, sizeof(recordCount));
  output.close();
  SdMan.remove(kIndexPath);
  if (!SdMan.rename(kTemporaryPath, kIndexPath)) {
    SdMan.remove(kTemporaryPath);
    return false;
  }
  if (progress) progress(total, total, "Authors generated");
  return true;
}

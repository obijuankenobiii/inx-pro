#include "MetadataIndex.h"

#include <Arduino.h>
#include <Epub.h>
#include <SDCardManager.h>
#include <Xtc.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <strings.h>
#include <utility>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "Epub/PsramAllocator.h"
#include "util/LibraryIndex.h"

namespace {
constexpr char kDirectory[] = "/.metadata/library";
constexpr char kIndexMagic[] = "MID1";
constexpr uint8_t kIndexVersion = 2;
constexpr uint8_t kLegacyIndexVersion = 1;
constexpr uint32_t kMaxGroups = 10000;
constexpr uint32_t kMaxBooksPerIndex = 100000;
constexpr uint32_t kMaxRawMetadataBytes = 64 * 1024;

using PsramString = EpubPsramString;

struct Record {
  PsramString key;
  PsramString label;
  PsramString path;
  PsramString title;
  PsramString author;
  PsramString order;
};
using Records = std::vector<Record, EpubPsramAllocator<Record>>;

struct GroupInfo {
  PsramString key;
  PsramString label;
  size_t first = 0;
  uint32_t count = 0;
  uint32_t offset = 0;
  uint32_t directoryOffset = 0;
};
using Groups = std::vector<GroupInfo, EpubPsramAllocator<GroupInfo>>;

struct HashSlot {
  uint32_t hash = 0;
  uint32_t directoryOffset = 0;
};
using HashSlots = std::vector<HashSlot, EpubPsramAllocator<HashSlot>>;

uint32_t groupHash(const std::string& value) {
  uint32_t hash = 2166136261u;
  for (const unsigned char ch : value) {
    hash ^= ch;
    hash *= 16777619u;
  }
  return hash;
}

bool supportedEpub(const char* name) {
  const size_t length = std::strlen(name);
  return length >= 5 && strcasecmp(name + length - 5, ".epub") == 0;
}

bool supportedXtc(const char* name) {
  const size_t length = std::strlen(name);
  return (length >= 4 && strcasecmp(name + length - 4, ".xtc") == 0) ||
         (length >= 5 && strcasecmp(name + length - 5, ".xtch") == 0);
}

bool skipDirectory(const char* name) {
  return !name || name[0] == '.' || strcasecmp(name, "sleep") == 0 || strcasecmp(name, "fonts") == 0 ||
         strcasecmp(name, "dictionaries") == 0 || strcasecmp(name, "System Volume Information") == 0;
}

std::string normalized(const std::string& value) {
  std::string result;
  result.reserve(value.size());
  bool pendingSpace = false;
  for (const unsigned char ch : value) {
    if (std::isspace(ch)) {
      pendingSpace = !result.empty();
      continue;
    }
    if (pendingSpace) result.push_back(' ');
    pendingSpace = false;
    result.push_back(static_cast<char>(std::tolower(ch)));
  }
  return result;
}

std::string localName(const std::string& value) {
  const size_t colon = value.find_last_of(':');
  std::string result = colon == std::string::npos ? value : value.substr(colon + 1);
  std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return result;
}

std::string getAuthorKey(const std::string& value) {
  const size_t comma = value.find(',');
  const std::string ordered = comma == std::string::npos ? value : value.substr(comma + 1) + " " + value.substr(0, comma);
  std::vector<std::string> tokens;
  std::string token;
  for (const unsigned char ch : ordered) {
    if (std::isalnum(ch)) {
      token.push_back(static_cast<char>(std::tolower(ch)));
    } else if (!token.empty()) {
      tokens.push_back(std::move(token));
      token.clear();
    }
  }
  if (!token.empty()) tokens.push_back(std::move(token));
  while (tokens.size() > 1) {
    const std::string& last = tokens.back();
    if (last != "jr" && last != "sr" && last != "ii" && last != "iii" && last != "iv" && last != "v") break;
    tokens.pop_back();
  }
  if (tokens.empty()) return {};
  if (tokens.size() == 1) return tokens.front();
  return tokens.front() + "|" + tokens.back();
}

std::string indexPath(const MetadataIndex::Kind kind) {
  const char* fileName = "";
  switch (kind) {
    case MetadataIndex::Kind::Authors: fileName = "authors.idx"; break;
    case MetadataIndex::Kind::Series: fileName = "series.idx"; break;
    case MetadataIndex::Kind::Tags: fileName = "tags.idx"; break;
    case MetadataIndex::Kind::Publishers: fileName = "publishers.idx"; break;
    case MetadataIndex::Kind::Languages: fileName = "languages.idx"; break;
    case MetadataIndex::Kind::Ratings: fileName = "ratings.idx"; break;
    case MetadataIndex::Kind::PublicationDates: fileName = "publication_dates.idx"; break;
    case MetadataIndex::Kind::Identifiers: fileName = "identifiers.idx"; break;
    case MetadataIndex::Kind::TitleSorts: fileName = "title_sorts.idx"; break;
    case MetadataIndex::Kind::AuthorSorts: fileName = "author_sorts.idx"; break;
  }
  return std::string(kDirectory) + "/" + fileName;
}

const char* kindLabel(const MetadataIndex::Kind kind) {
  switch (kind) {
    case MetadataIndex::Kind::Authors: return "Authors";
    case MetadataIndex::Kind::Series: return "Series";
    case MetadataIndex::Kind::Tags: return "Tags";
    case MetadataIndex::Kind::Publishers: return "Publishers";
    case MetadataIndex::Kind::Languages: return "Languages";
    case MetadataIndex::Kind::Ratings: return "Ratings";
    case MetadataIndex::Kind::PublicationDates: return "Dates";
    case MetadataIndex::Kind::Identifiers: return "Identifiers";
    case MetadataIndex::Kind::TitleSorts: return "Title sort";
    case MetadataIndex::Kind::AuthorSorts: return "Author sort";
  }
  return "Metadata";
}

bool writeShortString(FsFile& file, const std::string& value) {
  if (value.size() > UINT16_MAX) return false;
  const uint16_t length = static_cast<uint16_t>(value.size());
  return file.write(reinterpret_cast<const uint8_t*>(&length), sizeof(length)) == sizeof(length) &&
         (length == 0 || file.write(reinterpret_cast<const uint8_t*>(value.data()), length) == length);
}

bool readShortString(FsFile& file, std::string& value) {
  uint16_t length = 0;
  if (file.position() > file.size() ||
      file.read(reinterpret_cast<uint8_t*>(&length), sizeof(length)) != sizeof(length) ||
      file.position() > file.size() || static_cast<uint32_t>(length) > file.size() - file.position()) {
    return false;
  }
  value.resize(length);
  return length == 0 || file.read(reinterpret_cast<uint8_t*>(&value[0]), length) == length;
}

bool writeLongString(FsFile& file, const std::string& value) {
  if (value.size() > kMaxRawMetadataBytes) return false;
  const uint32_t length = static_cast<uint32_t>(value.size());
  return file.write(reinterpret_cast<const uint8_t*>(&length), sizeof(length)) == sizeof(length) &&
         (length == 0 || file.write(reinterpret_cast<const uint8_t*>(value.data()), length) == length);
}

bool readLongString(FsFile& file, std::string& value) {
  uint32_t length = 0;
  if (file.position() > file.size() ||
      file.read(reinterpret_cast<uint8_t*>(&length), sizeof(length)) != sizeof(length) ||
      file.position() > file.size() || length > kMaxRawMetadataBytes || length > file.size() - file.position()) {
    return false;
  }
  value.resize(length);
  return length == 0 || file.read(reinterpret_cast<uint8_t*>(&value[0]), length) == length;
}

float seriesNumber(const PsramString& value) {
  if (value.empty()) return std::numeric_limits<float>::max();
  char* end = nullptr;
  const float result = std::strtof(value.c_str(), &end);
  return end == value.c_str() || !std::isfinite(result) ? std::numeric_limits<float>::max() : result;
}

int compareFolded(const PsramString& left, const PsramString& right) {
  const size_t common = std::min(left.size(), right.size());
  for (size_t i = 0; i < common; ++i) {
    const auto leftChar = static_cast<unsigned char>(left[i]);
    const auto rightChar = static_cast<unsigned char>(right[i]);
    const int difference = std::tolower(leftChar) - std::tolower(rightChar);
    if (difference != 0) return difference;
  }
  if (left.size() == right.size()) return 0;
  return left.size() < right.size() ? -1 : 1;
}

bool recordLess(const Record& left, const Record& right, const MetadataIndex::Kind kind) {
  if (left.key != right.key) return left.key < right.key;
  if (kind == MetadataIndex::Kind::Series) {
    const float leftOrder = seriesNumber(left.order);
    const float rightOrder = seriesNumber(right.order);
    if (leftOrder != rightOrder) return leftOrder < rightOrder;
  }
  const int titleOrder = compareFolded(left.title, right.title);
  if (titleOrder != 0) return titleOrder < 0;
  return left.path < right.path;
}

bool addRecord(Records& records, const std::string& groupKey, const std::string& label,
               const std::string& path, const std::string& title, const std::string& author,
               const std::string& order) {
  if (groupKey.empty() || path.empty()) return true;
  if (records.size() >= kMaxBooksPerIndex) return false;
  Record record;
  record.key = groupKey.c_str();
  record.label = label.c_str();
  record.path = path.c_str();
  record.title = title.c_str();
  record.author = author.c_str();
  record.order = order.c_str();
  records.push_back(std::move(record));
  return true;
}

bool writeGroupedIndex(const MetadataIndex::Kind kind, Records& records,
                       const std::function<bool()>& shouldCancel) {
  std::sort(records.begin(), records.end(), [kind](const Record& a, const Record& b) { return recordLess(a, b, kind); });
  Groups groups;
  for (size_t i = 0; i < records.size();) {
    size_t end = i + 1;
    while (end < records.size() && records[end].key == records[i].key) ++end;
    GroupInfo group;
    group.key = records[i].key;
    group.label = records[i].label;
    group.first = i;
    group.count = static_cast<uint32_t>(end - i);
    groups.push_back(std::move(group));
    i = end;
  }
  if (groups.size() > kMaxGroups || (shouldCancel && shouldCancel())) return false;

  const std::string path = indexPath(kind);
  const std::string tempPath = path + ".tmp";
  FsFile output;
  if (!SdMan.openFileForWrite("MDX", tempPath, output)) return false;
  const uint32_t groupCount = static_cast<uint32_t>(groups.size());
  uint32_t hashCapacity = 2;
  while (hashCapacity < groupCount * 2 && hashCapacity < (1u << 30)) hashCapacity <<= 1;
  if (hashCapacity < groupCount * 2) {
    output.close();
    SdMan.remove(tempPath.c_str());
    return false;
  }

  constexpr uint32_t headerSize = sizeof(kIndexMagic) - 1 + sizeof(kIndexVersion) + sizeof(uint8_t) +
                                 sizeof(uint32_t) * 3;
  const uint32_t directoryStart = headerSize + hashCapacity * sizeof(HashSlot);
  uint32_t directoryOffset = directoryStart;
  for (GroupInfo& group : groups) {
    if (group.key.size() > UINT16_MAX || group.label.size() > UINT16_MAX) {
      output.close();
      SdMan.remove(tempPath.c_str());
      return false;
    }
    group.directoryOffset = directoryOffset;
    directoryOffset += sizeof(uint16_t) * 2 + static_cast<uint32_t>(group.key.size() + group.label.size()) +
                       sizeof(group.count) + sizeof(group.offset);
  }
  uint32_t dataOffset = directoryOffset;
  for (GroupInfo& group : groups) {
    group.offset = dataOffset;
    for (size_t i = group.first; i < group.first + group.count; ++i) {
      const Record& record = records[i];
      dataOffset += sizeof(uint16_t) * 4 + static_cast<uint32_t>(record.path.size() + record.title.size() +
                                                                  record.author.size() + record.order.size());
    }
  }

  HashSlots hashSlots(hashCapacity);
  for (const GroupInfo& group : groups) {
    const uint32_t hash = groupHash(std::string(group.key.c_str()));
    uint32_t slot = hash & (hashCapacity - 1);
    while (hashSlots[slot].directoryOffset != 0) slot = (slot + 1) & (hashCapacity - 1);
    hashSlots[slot] = HashSlot{hash, group.directoryOffset};
  }

  bool ok = output.write(reinterpret_cast<const uint8_t*>(kIndexMagic), sizeof(kIndexMagic) - 1) == sizeof(kIndexMagic) - 1;
  ok = ok && output.write(&kIndexVersion, sizeof(kIndexVersion)) == sizeof(kIndexVersion);
  const uint8_t storedKind = static_cast<uint8_t>(kind);
  ok = ok && output.write(&storedKind, sizeof(storedKind)) == sizeof(storedKind);
  ok = ok && output.write(reinterpret_cast<const uint8_t*>(&groupCount), sizeof(groupCount)) == sizeof(groupCount);
  ok = ok && output.write(reinterpret_cast<const uint8_t*>(&hashCapacity), sizeof(hashCapacity)) == sizeof(hashCapacity);
  ok = ok && output.write(reinterpret_cast<const uint8_t*>(&directoryStart), sizeof(directoryStart)) == sizeof(directoryStart);
  for (const HashSlot& slot : hashSlots) {
    ok = ok && output.write(reinterpret_cast<const uint8_t*>(&slot.hash), sizeof(slot.hash)) == sizeof(slot.hash);
    ok = ok && output.write(reinterpret_cast<const uint8_t*>(&slot.directoryOffset), sizeof(slot.directoryOffset)) ==
                   sizeof(slot.directoryOffset);
    if (!ok) break;
  }

  for (const GroupInfo& group : groups) {
    ok = ok && writeShortString(output, std::string(group.key.c_str()));
    ok = ok && writeShortString(output, std::string(group.label.c_str()));
    ok = ok && output.write(reinterpret_cast<const uint8_t*>(&group.count), sizeof(group.count)) == sizeof(group.count);
    ok = ok && output.write(reinterpret_cast<const uint8_t*>(&group.offset), sizeof(group.offset)) == sizeof(group.offset);
    if (!ok) break;
  }
  for (const GroupInfo& group : groups) {
    for (size_t i = group.first; ok && i < group.first + group.count; ++i) {
      const Record& record = records[i];
      ok = writeShortString(output, std::string(record.path.c_str())) &&
           writeShortString(output, std::string(record.title.c_str())) &&
           writeShortString(output, std::string(record.author.c_str())) &&
           writeShortString(output, std::string(record.order.c_str()));
      if ((i & 31) == 0) yield();
    }
  }
  output.close();
  if (!ok || (shouldCancel && shouldCancel())) {
    SdMan.remove(tempPath.c_str());
    return false;
  }
  SdMan.remove(path.c_str());
  if (!SdMan.rename(tempPath.c_str(), path.c_str())) {
    SdMan.remove(tempPath.c_str());
    return false;
  }
  return true;
}

std::string fieldValue(const BookMetadataCache::BookMetadata& metadata, const MetadataIndex::Kind kind) {
  switch (kind) {
    case MetadataIndex::Kind::Authors: return metadata.author;
    case MetadataIndex::Kind::Series: return metadata.series;
    case MetadataIndex::Kind::Publishers: return metadata.publisher;
    case MetadataIndex::Kind::Languages: return metadata.language;
    case MetadataIndex::Kind::Ratings: return metadata.rating;
    case MetadataIndex::Kind::PublicationDates: return metadata.publicationDate;
    case MetadataIndex::Kind::TitleSorts: return metadata.titleSort;
    case MetadataIndex::Kind::AuthorSorts: return metadata.authorSort;
    default: return {};
  }
}

const std::string* metadataAttribute(const BookMetadataCache::MetadataField& field, const char* wanted) {
  for (const BookMetadataCache::MetadataAttribute& attribute : field.attributes) {
    if (localName(attribute.name) == wanted) return &attribute.value;
  }
  return nullptr;
}

std::string rawCalibreSeriesValue(const BookMetadataCache::BookMetadata& metadata, const bool index) {
  const char* calibreName = index ? "series_index" : "series";
  const char* epubProperty = index ? "group-position" : "belongs-to-collection";
  for (const BookMetadataCache::MetadataField& field : metadata.fields) {
    if (localName(field.name) != "meta") continue;
    const std::string* name = metadataAttribute(field, "name");
    const std::string* property = metadataAttribute(field, "property");
    if (name && localName(*name) == calibreName) {
      const std::string* content = metadataAttribute(field, "content");
      return content ? *content : field.value;
    }
    if (property && localName(*property) == epubProperty) return field.value;
  }
  return {};
}

std::string groupKeyFor(const std::string& label, const MetadataIndex::Kind kind) {
  return kind == MetadataIndex::Kind::Authors ? getAuthorKey(label) : normalized(label);
}

bool writeFullMetadata(FsFile& output, const std::string& path, const BookMetadataCache::BookMetadata& metadata) {
  if (!writeLongString(output, path) || !writeLongString(output, metadata.title) ||
      !writeLongString(output, metadata.author) || !writeLongString(output, metadata.titleSort) ||
      !writeLongString(output, metadata.authorSort) || !writeLongString(output, metadata.series) ||
      !writeLongString(output, metadata.seriesIndex) || !writeLongString(output, metadata.rating) ||
      !writeLongString(output, metadata.publisher) || !writeLongString(output, metadata.publicationDate) ||
      !writeLongString(output, metadata.calibreTimestamp) || !writeLongString(output, metadata.calibreUuid) ||
      !writeLongString(output, metadata.language) || !writeLongString(output, metadata.description)) {
    return false;
  }
  const uint16_t tagCount = static_cast<uint16_t>(std::min<size_t>(metadata.tags.size(), 256));
  if (output.write(reinterpret_cast<const uint8_t*>(&tagCount), sizeof(tagCount)) != sizeof(tagCount)) return false;
  for (uint16_t i = 0; i < tagCount; ++i) {
    if (!writeLongString(output, metadata.tags[i])) return false;
  }
  const uint16_t fieldCount = static_cast<uint16_t>(std::min<size_t>(metadata.fields.size(), 128));
  if (output.write(reinterpret_cast<const uint8_t*>(&fieldCount), sizeof(fieldCount)) != sizeof(fieldCount)) return false;
  for (uint16_t i = 0; i < fieldCount; ++i) {
    const auto& field = metadata.fields[i];
    if (!writeLongString(output, field.name) || !writeLongString(output, field.value)) return false;
    const uint8_t attributeCount = static_cast<uint8_t>(std::min<size_t>(field.attributes.size(), 16));
    if (output.write(reinterpret_cast<const uint8_t*>(&attributeCount), sizeof(attributeCount)) != sizeof(attributeCount)) {
      return false;
    }
    for (uint8_t j = 0; j < attributeCount; ++j) {
      if (!writeLongString(output, field.attributes[j].name) || !writeLongString(output, field.attributes[j].value)) {
        return false;
      }
    }
  }
  return true;
}

template <typename Visitor>
bool scanLibrary(const bool includeXtc, const std::function<void(int, int, const char*)>& progress,
                 const std::function<bool()>& shouldCancel, Visitor&& visitor) {
  int total = 0;
  FsFile root = SdMan.open("/");
  if (root) {
    total = LibraryIndex::countBooks(root);
    root.close();
  }
  int current = 0;

  std::function<bool(const std::string&)> scan = [&](const std::string& dirPath) {
    if (shouldCancel && shouldCancel()) return false;
    FsFile directory = SdMan.open(dirPath.c_str());
    if (!directory || !directory.isDirectory()) {
      if (directory) directory.close();
      return true;
    }
    char name[256] = {};
    while (!(shouldCancel && shouldCancel())) {
      FsFile entry = directory.openNextFile();
      if (!entry) break;
      entry.getName(name, sizeof(name));
      if (skipDirectory(name)) {
        entry.close();
        continue;
      }
      const bool isDirectory = entry.isDirectory();
      const std::string fullPath = dirPath == "/" ? "/" + std::string(name) : dirPath + "/" + name;
      entry.close();
      if (isDirectory) {
        if (!scan(fullPath)) {
          directory.close();
          return false;
        }
        continue;
      }
      const bool isEpub = supportedEpub(name);
      const bool isXtc = includeXtc && supportedXtc(name);
      if (!isEpub && !isXtc) continue;

      if (isEpub) {
        Epub epub(fullPath, "/.metadata/epub");
        if (epub.load(false) || epub.load(true)) {
          if (!visitor(fullPath, epub.getTitle(), epub.getAuthor(), epub.getBookMetadata(), false)) {
            directory.close();
            return false;
          }
        }
      } else {
        Xtc xtc(fullPath, "/.metadata/xtc");
        const bool loaded = xtc.load();
        const std::string title = loaded ? xtc.getTitle() : std::string();
        const std::string author = loaded ? xtc.getAuthor() : std::string();
        if (!visitor(fullPath, title, author, BookMetadataCache::BookMetadata{}, true)) {
          directory.close();
          return false;
        }
      }
      ++current;
      if (progress) progress(current, total, name);
      if ((current & 3) == 0) vTaskDelay(pdMS_TO_TICKS(1));
    }
    directory.close();
    return !(shouldCancel && shouldCancel());
  };

  return scan("/");
}

bool generateGrouped(const MetadataIndex::Kind kind, const bool includeXtc,
                     const std::function<void(int, int, const char*)>& progress,
                     const std::function<bool()>& shouldCancel) {
  Records records;
  const bool completed = scanLibrary(includeXtc, progress, shouldCancel,
      [&records, kind](const std::string& path, const std::string& title, const std::string& author,
                       const BookMetadataCache::BookMetadata& metadata, const bool isXtc) {
        if (kind == MetadataIndex::Kind::Tags) {
          for (const std::string& tag : metadata.tags) {
            if (!addRecord(records, groupKeyFor(tag, kind), tag, path, title, author, "")) return false;
          }
        } else if (kind == MetadataIndex::Kind::Identifiers) {
          for (const auto& field : metadata.fields) {
            if (localName(field.name) == "identifier" && !field.value.empty()) {
              if (!addRecord(records, groupKeyFor(field.value, kind), field.value, path, title, author, "")) return false;
            }
          }
        } else {
          std::string value = kind == MetadataIndex::Kind::Authors && isXtc
                                  ? author
                                  : fieldValue(metadata, kind);
          if (kind == MetadataIndex::Kind::Series && value.empty()) {
            value = rawCalibreSeriesValue(metadata, false);
          }
          if (!value.empty()) {
            const std::string key = groupKeyFor(value, kind);
            std::string order = kind == MetadataIndex::Kind::Series ? metadata.seriesIndex : "";
            if (kind == MetadataIndex::Kind::Series && order.empty()) {
              order = rawCalibreSeriesValue(metadata, true);
            }
            if (!addRecord(records, key, value, path, title, author, order)) return false;
          }
        }
        return true;
      });
  return completed && writeGroupedIndex(kind, records, shouldCancel);
}

bool generateAllMetadata(const std::function<void(int, int, const char*)>& progress,
                         const std::function<bool()>& shouldCancel) {
  const std::string index = std::string(kDirectory) + "/all_metadata.idx";
  const std::string temp = index + ".tmp";
  FsFile output;
  if (!SdMan.openFileForWrite("MDX", temp, output)) return false;
  constexpr char magic[] = "BMDA";
  const uint8_t version = 1;
  uint32_t count = 0;
  bool ok = output.write(reinterpret_cast<const uint8_t*>(magic), sizeof(magic) - 1) == sizeof(magic) - 1 &&
            output.write(&version, sizeof(version)) == sizeof(version);
  const uint32_t countOffset = output.position();
  ok = ok && output.write(reinterpret_cast<const uint8_t*>(&count), sizeof(count)) == sizeof(count);
  const bool completed = scanLibrary(false, progress, shouldCancel,
      [&output, &count](const std::string& path, const std::string&, const std::string&,
                        const BookMetadataCache::BookMetadata& metadata, const bool) {
        if (!writeFullMetadata(output, path, metadata)) return false;
        ++count;
        return true;
      });
  if (ok && completed) {
    output.seek(countOffset);
    ok = output.write(reinterpret_cast<const uint8_t*>(&count), sizeof(count)) == sizeof(count);
  } else {
    ok = false;
  }
  output.close();
  if (!ok || (shouldCancel && shouldCancel())) {
    SdMan.remove(temp.c_str());
    return false;
  }
  SdMan.remove(index.c_str());
  if (!SdMan.rename(temp.c_str(), index.c_str())) {
    SdMan.remove(temp.c_str());
    return false;
  }
  return true;
}

bool generateSelected(const MetadataIndex::Kind kind, const std::function<void(int, int, const char*)>& progress,
                      const std::function<bool()>& shouldCancel) {
  const bool includeXtc = kind == MetadataIndex::Kind::Authors;
  return generateGrouped(kind, includeXtc, progress, shouldCancel);
}
}

bool MetadataIndex::hasIndex(const Kind kind) { return SdMan.exists(indexPath(kind).c_str()); }

std::string MetadataIndex::authorKey(const std::string& author) { return getAuthorKey(author); }

bool MetadataIndex::loadGroups(const Kind kind, std::vector<Group>& groups) {
  groups.clear();
  FsFile file = SdMan.open(indexPath(kind).c_str(), O_READ);
  if (!file) return false;
  char magic[sizeof(kIndexMagic) - 1] = {};
  uint8_t version = 0;
  uint8_t storedKind = 0;
  uint32_t count = 0;
  bool ok = file.read(magic, sizeof(magic)) == sizeof(magic) && std::memcmp(magic, kIndexMagic, sizeof(magic)) == 0 &&
            file.read(&version, sizeof(version)) == sizeof(version) &&
            (version == kIndexVersion || version == kLegacyIndexVersion) &&
            file.read(&storedKind, sizeof(storedKind)) == sizeof(storedKind) && storedKind == static_cast<uint8_t>(kind) &&
            file.read(reinterpret_cast<uint8_t*>(&count), sizeof(count)) == sizeof(count) && count <= kMaxGroups;
  if (ok && version == kIndexVersion) {
    uint32_t hashCapacity = 0;
    uint32_t directoryStart = 0;
    ok = file.read(reinterpret_cast<uint8_t*>(&hashCapacity), sizeof(hashCapacity)) == sizeof(hashCapacity) &&
         file.read(reinterpret_cast<uint8_t*>(&directoryStart), sizeof(directoryStart)) == sizeof(directoryStart) &&
         hashCapacity >= 2 && (hashCapacity & (hashCapacity - 1)) == 0 &&
         directoryStart == file.position() + hashCapacity * sizeof(HashSlot) && directoryStart <= file.size() &&
         file.seek(directoryStart);
  }
  if (!ok) {
    file.close();
    return false;
  }
  groups.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    Group group;
    if (!readShortString(file, group.key) || !readShortString(file, group.label) ||
        file.read(reinterpret_cast<uint8_t*>(&group.count), sizeof(group.count)) != sizeof(group.count) ||
        file.read(reinterpret_cast<uint8_t*>(&group.offset), sizeof(group.offset)) != sizeof(group.offset) ||
        group.count > kMaxBooksPerIndex || group.offset > file.size()) {
      groups.clear();
      file.close();
      return false;
    }
    groups.push_back(std::move(group));
  }
  file.close();
  return true;
}

bool MetadataIndex::findGroup(const Kind kind, const std::string& key, Group& group) {
  if (key.empty()) return false;
  FsFile file = SdMan.open(indexPath(kind).c_str(), O_READ);
  if (!file) return false;
  char magic[sizeof(kIndexMagic) - 1] = {};
  uint8_t version = 0;
  uint8_t storedKind = 0;
  uint32_t count = 0;
  bool ok = file.read(magic, sizeof(magic)) == sizeof(magic) && std::memcmp(magic, kIndexMagic, sizeof(magic)) == 0 &&
            file.read(&version, sizeof(version)) == sizeof(version) &&
            (version == kIndexVersion || version == kLegacyIndexVersion) &&
            file.read(&storedKind, sizeof(storedKind)) == sizeof(storedKind) && storedKind == static_cast<uint8_t>(kind) &&
            file.read(reinterpret_cast<uint8_t*>(&count), sizeof(count)) == sizeof(count) && count <= kMaxGroups;
  if (!ok) {
    file.close();
    return false;
  }

  if (version == kLegacyIndexVersion) {
    file.close();
    std::vector<Group> groups;
    if (!loadGroups(kind, groups)) return false;
    for (Group& candidate : groups) {
      if (candidate.key == key) {
        group = std::move(candidate);
        return true;
      }
    }
    return false;
  }

  uint32_t hashCapacity = 0;
  uint32_t directoryStart = 0;
  ok = file.read(reinterpret_cast<uint8_t*>(&hashCapacity), sizeof(hashCapacity)) == sizeof(hashCapacity) &&
       file.read(reinterpret_cast<uint8_t*>(&directoryStart), sizeof(directoryStart)) == sizeof(directoryStart) &&
       hashCapacity >= 2 && (hashCapacity & (hashCapacity - 1)) == 0 && hashCapacity <= (1u << 20) &&
       directoryStart == file.position() + hashCapacity * sizeof(HashSlot) && directoryStart <= file.size();
  if (!ok) {
    file.close();
    return false;
  }

  constexpr uint32_t headerSize = sizeof(kIndexMagic) - 1 + sizeof(kIndexVersion) + sizeof(uint8_t) +
                                 sizeof(uint32_t) * 3;
  const uint32_t wantedHash = groupHash(key);
  uint32_t slotIndex = wantedHash & (hashCapacity - 1);
  for (uint32_t probe = 0; probe < hashCapacity; ++probe) {
    const uint32_t slotOffset = headerSize + slotIndex * sizeof(HashSlot);
    uint32_t storedHash = 0;
    uint32_t recordOffset = 0;
    if (!file.seek(slotOffset) ||
        file.read(reinterpret_cast<uint8_t*>(&storedHash), sizeof(storedHash)) != sizeof(storedHash) ||
        file.read(reinterpret_cast<uint8_t*>(&recordOffset), sizeof(recordOffset)) != sizeof(recordOffset)) {
      file.close();
      return false;
    }
    if (recordOffset == 0) {
      file.close();
      return false;
    }
    if (storedHash == wantedHash && recordOffset < file.size() && file.seek(recordOffset)) {
      Group candidate;
      if (!readShortString(file, candidate.key) || !readShortString(file, candidate.label) ||
          file.read(reinterpret_cast<uint8_t*>(&candidate.count), sizeof(candidate.count)) != sizeof(candidate.count) ||
          file.read(reinterpret_cast<uint8_t*>(&candidate.offset), sizeof(candidate.offset)) != sizeof(candidate.offset) ||
          candidate.count > kMaxBooksPerIndex || candidate.offset > file.size()) {
        file.close();
        return false;
      }
      if (candidate.key == key) {
        group = std::move(candidate);
        file.close();
        return true;
      }
    }
    slotIndex = (slotIndex + 1) & (hashCapacity - 1);
  }
  file.close();
  return false;
}

bool MetadataIndex::loadGroup(const Kind kind, const Group& group, std::vector<Entry>& entries, const size_t maxEntries) {
  entries.clear();
  FsFile file = SdMan.open(indexPath(kind).c_str(), O_READ);
  if (!file || !file.seek(group.offset)) {
    if (file) file.close();
    return false;
  }
  const uint32_t count = static_cast<uint32_t>(std::min<size_t>(group.count, maxEntries));
  entries.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    Entry entry;
    if (!readShortString(file, entry.path) || !readShortString(file, entry.title) ||
        !readShortString(file, entry.author) || !readShortString(file, entry.order)) {
      entries.clear();
      file.close();
      return false;
    }
    entries.push_back(std::move(entry));
  }
  file.close();
  return true;
}

bool MetadataIndex::generate(const Options& options, const std::function<void(int, int, const char*)>& progress,
                             const std::function<bool()>& shouldCancel) {
  if (!options.authors && !options.series && !options.tags && !options.publishers && !options.languages &&
      !options.ratings && !options.publicationDates && !options.identifiers && !options.titleSorts &&
      !options.authorSorts && !options.completeRecords) {
    return false;
  }
  if (!SdMan.exists("/.metadata")) SdMan.mkdir("/.metadata");
  if (!SdMan.exists(kDirectory)) SdMan.mkdir(kDirectory);

  const Kind kinds[] = {Kind::Authors, Kind::Series, Kind::Tags, Kind::Publishers, Kind::Languages, Kind::Ratings,
                        Kind::PublicationDates, Kind::Identifiers, Kind::TitleSorts, Kind::AuthorSorts};
  for (const Kind kind : kinds) {
    bool selected = false;
    switch (kind) {
      case Kind::Authors: selected = options.authors; break;
      case Kind::Series: selected = options.series; break;
      case Kind::Tags: selected = options.tags; break;
      case Kind::Publishers: selected = options.publishers; break;
      case Kind::Languages: selected = options.languages; break;
      case Kind::Ratings: selected = options.ratings; break;
      case Kind::PublicationDates: selected = options.publicationDates; break;
      case Kind::Identifiers: selected = options.identifiers; break;
      case Kind::TitleSorts: selected = options.titleSorts; break;
      case Kind::AuthorSorts: selected = options.authorSorts; break;
    }
    if (selected) {
      const auto stageProgress = [&progress, kind](const int current, const int total, const char* path) {
        if (!progress) return;
        std::string label = kindLabel(kind);
        if (path && path[0]) label += ": " + std::string(path);
        progress(current, total, label.c_str());
      };
      if (!generateSelected(kind, stageProgress, shouldCancel)) return false;
    }
  }
  if (options.completeRecords) {
    const auto fullMetadataProgress = [&progress](const int current, const int total, const char* path) {
      if (!progress) return;
      std::string label = "Complete records";
      if (path && path[0]) label += ": " + std::string(path);
      progress(current, total, label.c_str());
    };
    if (!generateAllMetadata(fullMetadataProgress, shouldCancel)) return false;
  }
  return !(shouldCancel && shouldCancel());
}

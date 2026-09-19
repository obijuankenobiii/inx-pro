#pragma once

#include <functional>
#include <cstdint>
#include <string>
#include <vector>

/** Pre-grouped, on-device indexes derived from per-book EPUB cache metadata. */
class MetadataIndex final {
 public:
  enum class Kind : uint8_t {
    Authors = 1,
    Series,
    Tags,
    Publishers,
    Languages,
    Ratings,
    PublicationDates,
    Identifiers,
    TitleSorts,
    AuthorSorts,
  };

  struct Group {
    std::string key;
    std::string label;
    uint32_t count = 0;
    uint32_t offset = 0;
  };

  struct Entry {
    std::string path;
    std::string title;
    std::string author;
    std::string order;
  };

  struct Options {
    bool authors = false;
    bool series = false;
    bool tags = false;
    bool publishers = false;
    bool languages = false;
    bool ratings = false;
    bool publicationDates = false;
    bool identifiers = false;
    bool titleSorts = false;
    bool authorSorts = false;
    bool completeRecords = false;
  };

  static bool hasIndex(Kind kind);
  // Enumerating groups is linear in the number of displayed groups; locating a
  // known group in the current index format is expected O(1).
  static bool loadGroups(Kind kind, std::vector<Group>& groups);
  static bool findGroup(Kind kind, const std::string& key, Group& group);
  static bool loadGroup(Kind kind, const Group& group, std::vector<Entry>& entries,
                        size_t maxEntries = static_cast<size_t>(-1));
  static bool generate(const Options& options,
                       const std::function<void(int, int, const char*, const char*)>& progress,
                       const std::function<bool()>& shouldCancel = {});
  static std::string authorKey(const std::string& author);
};

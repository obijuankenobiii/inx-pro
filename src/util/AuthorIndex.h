#pragma once

#include <functional>
#include <string>
#include <vector>

/** Stores the author metadata generated for library books. */
class AuthorIndex final {
 public:
  struct Entry {
    std::string path;
    std::string author;
  };

  static bool hasIndex();
  static bool load(std::vector<Entry>& entries);
  static bool generate(const std::function<void(int, int, const char*)>& progress,
                       const std::function<bool()>& shouldCancel = {});
};

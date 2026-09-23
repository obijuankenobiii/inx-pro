#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

/** Lists and installs compressed StarDict dictionaries hosted in inx-store. */
class DictionaryPackageManager {
 public:
  struct Package {
    std::string name;
    std::string folder;
    std::string url;
    size_t size = 0;
  };

  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;

  static bool fetchAvailable(std::vector<Package>& packages, std::string& error);
  static bool install(const Package& package, std::string& error, ProgressCallback progress = nullptr);
  static bool remove(const Package& package, std::string& error);
  static bool isInstalled(const Package& package);

 private:
  DictionaryPackageManager() = delete;
};

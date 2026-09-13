#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

/** Installs and tracks optional, capability-based reader plugins. */
class PluginManager {
 public:
  struct Package {
    std::string id;
    std::string name;
    std::string description;
    std::string url;
    size_t size = 0;
  };

  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;

  static bool fetchAvailable(std::vector<Package>& packages, std::string& error);
  static bool install(const Package& package, std::string& error, ProgressCallback progress = nullptr);
  static bool installArchive(const std::string& archivePath, const std::string& id, std::string& error);
  static bool remove(const Package& package, std::string& error);
  static bool isInstalled(const Package& package);
  static bool isInstalled(const char* id);

 private:
  PluginManager() = delete;
};

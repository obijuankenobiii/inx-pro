#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

/** Lists and installs compiled SD-font packages hosted in the inx-font repository. */
class FontPackageManager {
 public:
  struct Package {
    std::string name;          // Display name. The selected tab supplies the bit-depth variant.
    std::string variant;       // Variant tab identifier: "1-bit" or "2-bit".
    std::string url;           // Raw GitHub ZIP URL.
    std::string installFamily; // Separate SD family so variants cannot overwrite each other.
    size_t size = 0;
  };

  static bool fetchAvailable(std::vector<Package>& packages, std::string& error);
  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;
  static bool install(const Package& package, std::string& error, ProgressCallback progress = nullptr);
  static bool remove(const Package& package, std::string& error);
  static bool isInstalled(const Package& package);

 private:
  FontPackageManager() = delete;
};

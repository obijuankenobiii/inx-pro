#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

/** Lists and installs language packages hosted in the inx-font repository.
 *
 * A language package is a single ZIP containing:
 *   lang/<code>/translate.yml
 *   lang/<code>/hyphenation.bin (optional)
 *   fonts/lang/<code>/*.bin (optional)
 *
 * The two parts are installed separately on the SD card so the translation
 * metadata and its font fallback files can be managed independently.
 */
class LanguagePackageManager {
 public:
  struct Package {
    std::string code;
    std::string name;
    std::string url;
    size_t size = 0;
  };

  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;

  static bool fetchAvailable(std::vector<Package>& packages, std::string& error);
  static bool install(const Package& package, std::string& error, ProgressCallback progress = nullptr);
  /** Installs a language ZIP already present on the SD card. */
  static bool installArchive(const std::string& archivePath, const std::string& code, std::string& error);
  static bool remove(const Package& package, std::string& error);
  static bool isInstalled(const Package& package);

 private:
  LanguagePackageManager() = delete;
};

#include "LanguagePackageManager.h"

#include <SDCardManager.h>

#include <cctype>

#include "../network/HttpDownloader.h"
#include "util/SdIoMutex.h"
#include "util/StringUtils.h"
#include <FsHelpers.h>
#include <ZipFile.h>

namespace {
constexpr char kRepositoryBase[] =
    "https://raw.githubusercontent.com/obijuankenobiii/inx-store/main/";
constexpr char kDownloadPath[] = "/.system/language-package.zip";
constexpr size_t kMaxPackageBytes = 5 * 1024 * 1024;
constexpr size_t kMaxExtractedBytes = 16 * 1024 * 1024;
constexpr size_t kMaxTranslationBytes = 128 * 1024;

struct StaticPackage {
  const char* code;
  const char* name;
  const char* path;
};

// CJK is a font-first package. Its YAML file is intentionally metadata-only
// until translated UI strings are added to the language repository.
constexpr StaticPackage kStaticPackages[] = {
    {"cjk", "CJK", "lang/cjk.zip"},
};

bool isSafeLanguageCode(const std::string& code) {
  if (code.empty() || code.size() > 32 || code == "." || code == "..") return false;
  if (!std::isalnum(static_cast<unsigned char>(code.front()))) return false;
  for (const unsigned char c : code) {
    if (!std::isalnum(c) && c != '-' && c != '_') return false;
  }
  return true;
}

bool isSafeBinName(const std::string& name) {
  if (!StringUtils::checkFileExtension(name, ".bin")) return false;
  if (name.empty() || name == "." || name == "..") return false;
  for (const char c : name) {
    if (c == '/' || c == '\\' || c == ':' || c < 32) return false;
  }
  return true;
}

bool isSafeYamlName(const std::string& name) {
  return name == "translate.yml" || name == "translate.yaml";
}

bool isSafeHyphenationName(const std::string& name) { return name == "hyphenation.bin"; }

std::string entryBaseName(const std::string& entryName) {
  const size_t slash = entryName.rfind('/');
  return slash == std::string::npos ? entryName : entryName.substr(slash + 1);
}

bool removeDirectoryTree(const std::string& path) {
  FsFile directory = SdMan.open(path.c_str());
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    return false;
  }

  bool removed = true;
  char name[128] = {};
  while (true) {
    FsFile entry = directory.openNextFile();
    if (!entry) break;
    entry.getName(name, sizeof(name));
    const bool isDirectory = entry.isDirectory();
    entry.close();

    const std::string childPath = path + "/" + name;
    if (isDirectory) {
      if (!removeDirectoryTree(childPath)) removed = false;
    } else if (!SdMan.remove(childPath.c_str())) {
      removed = false;
    }
  }
  directory.close();

  return removed && SdMan.removeDir(path.c_str());
}

bool writeZipEntry(ZipFile& zip, const std::string& entryName, const std::string& outputPath,
                   const size_t inflatedSize, std::string& error) {
  FsFile output;
  if (!SdMan.openFileForWrite("LANG", outputPath, output)) {
    error = "Could not create language file on SD card";
    return false;
  }

  const bool copied = zip.readFileToStream(entryName.c_str(), output, 1024, inflatedSize);
  output.close();
  if (!copied) {
    SdMan.remove(outputPath.c_str());
    error = "Could not extract language package";
    return false;
  }
  return true;
}

bool installArchiveLocked(const std::string& archivePath, const std::string& code, std::string& error) {
  error.clear();
  if (!isSafeLanguageCode(code)) {
    error = "Invalid language code";
    return false;
  }

  FsFile archiveFile = SdMan.open(archivePath.c_str());
  const size_t archiveSize = archiveFile ? archiveFile.size() : 0;
  if (archiveFile) archiveFile.close();
  if (archiveSize == 0 || archiveSize > kMaxPackageBytes) {
    error = "Invalid language package size";
    return false;
  }

  ZipFile zip{archivePath};
  if (!zip.open() || !zip.loadAllFileStatSlims()) {
    zip.close();
    error = "Language package is not a valid ZIP";
    return false;
  }

  const std::string languagePrefix = "lang/" + code + "/";
  const std::string fontPrefix = "fonts/lang/" + code + "/";
  std::string translationEntry;
  size_t translationSize = 0;
  size_t extractedBytes = 0;

  // Validate the complete archive before writing anything. This keeps a
  // malformed package from leaving a half-installed language behind.
  for (size_t i = 0; i < zip.entryCount(); ++i) {
    const char* name = zip.fileNameAt(i);
    if (!name) continue;
    const std::string entryName = FsHelpers::normalisePath(name);
    if (entryName.empty() || entryName.find("__MACOSX/") == 0) continue;

    const bool isTranslation = entryName.compare(0, languagePrefix.size(), languagePrefix) == 0 &&
                               isSafeYamlName(entryName.substr(languagePrefix.size()));
    const bool isHyphenation = entryName == languagePrefix + "hyphenation.bin";
    const bool isFont = entryName.compare(0, fontPrefix.size(), fontPrefix) == 0 &&
                        isSafeBinName(entryBaseName(entryName));
    if (!isTranslation && !isHyphenation && !isFont) continue;

    size_t inflatedSize = 0;
    if (!zip.getInflatedFileSize(entryName.c_str(), &inflatedSize) || inflatedSize == 0) {
      zip.close();
      error = "Language package contains an invalid file";
      return false;
    }
    if (isTranslation) {
      if (!translationEntry.empty() || inflatedSize > kMaxTranslationBytes) {
        zip.close();
        error = "Language package contains invalid translation metadata";
        return false;
      }
      translationEntry = entryName;
      translationSize = inflatedSize;
    } else if (isHyphenation) {
      if (!isSafeHyphenationName(entryName.substr(languagePrefix.size())) ||
          inflatedSize > kMaxExtractedBytes || extractedBytes + inflatedSize > kMaxExtractedBytes) {
        zip.close();
        error = "Language package contains invalid hyphenation data";
        return false;
      }
      extractedBytes += inflatedSize;
    } else {
      if (extractedBytes + inflatedSize > kMaxExtractedBytes) {
        zip.close();
        error = "Language package is too large after extraction";
        return false;
      }
      extractedBytes += inflatedSize;
    }
  }

  if (translationEntry.empty() || translationSize == 0) {
    zip.close();
    error = "Language package needs translate.yml";
    return false;
  }

  SdMan.mkdir("/system");
  SdMan.mkdir("/system/lang");
  SdMan.mkdir((std::string("/system/lang/") + code).c_str());
  SdMan.mkdir("/fonts");
  SdMan.mkdir("/fonts/lang");
  SdMan.mkdir((std::string("/fonts/lang/") + code).c_str());

  const std::string translationPath = "/system/lang/" + code + "/translate.yml";
  size_t writtenBytes = 0;
  for (size_t i = 0; i < zip.entryCount(); ++i) {
    const char* name = zip.fileNameAt(i);
    if (!name) continue;
    const std::string entryName = FsHelpers::normalisePath(name);
    if (entryName == translationEntry) {
      if (!writeZipEntry(zip, entryName, translationPath, translationSize, error)) {
        zip.close();
        return false;
      }
      writtenBytes += translationSize;
      continue;
    }
    if (entryName == languagePrefix + "hyphenation.bin") {
      size_t inflatedSize = 0;
      if (!zip.getInflatedFileSize(entryName.c_str(), &inflatedSize) ||
          !writeZipEntry(zip, entryName, "/system/lang/" + code + "/hyphenation.bin", inflatedSize, error)) {
        zip.close();
        return false;
      }
      writtenBytes += inflatedSize;
      continue;
    }
    if (entryName.compare(0, fontPrefix.size(), fontPrefix) != 0 ||
        !isSafeBinName(entryBaseName(entryName))) continue;

    size_t inflatedSize = 0;
    if (!zip.getInflatedFileSize(entryName.c_str(), &inflatedSize)) {
      zip.close();
      error = "Could not read language font metadata";
      return false;
    }
    const std::string outputPath = "/fonts/lang/" + code + "/" +
                                   StringUtils::sanitizeFilename(entryBaseName(entryName), 96);
    if (!writeZipEntry(zip, entryName, outputPath, inflatedSize, error)) {
      zip.close();
      return false;
    }
    writtenBytes += inflatedSize;
  }

  zip.close();
  if (writtenBytes == 0) {
    error = "Language package did not install any files";
    return false;
  }
  return true;
}
}  // namespace

bool LanguagePackageManager::fetchAvailable(std::vector<Package>& packages, std::string& error) {
  packages.clear();
  error.clear();
  packages.reserve(sizeof(kStaticPackages) / sizeof(kStaticPackages[0]));
  for (const StaticPackage& item : kStaticPackages) {
    packages.push_back({item.code, item.name, std::string(kRepositoryBase) + item.path, 0});
  }
  return true;
}

bool LanguagePackageManager::isInstalled(const Package& package) {
  if (!SdMan.ready() || !isSafeLanguageCode(package.code)) return false;

  SdIoMutex::Lock ioLock;
  const std::string codePath = "/system/lang/" + package.code;
  const std::string translationPath = codePath + "/translate.yml";
  if (!SdMan.exists(translationPath.c_str())) return false;

  return true;
}

bool LanguagePackageManager::install(const Package& package, std::string& error, ProgressCallback progress) {
  error.clear();
  if (!isSafeLanguageCode(package.code) || package.url.empty()) {
    error = "Invalid language package";
    return false;
  }
  if (package.size > kMaxPackageBytes) {
    error = "Language package is larger than 5 MB";
    return false;
  }
  if (!SdMan.ready()) {
    error = "SD card is not ready";
    return false;
  }

  SdIoMutex::Lock ioLock;
  SdMan.mkdir("/.system");
  if (HttpDownloader::downloadToFile(package.url, kDownloadPath, "", "", progress) != HttpDownloader::OK) {
    error = "Language download failed";
    return false;
  }

  const bool installed = installArchiveLocked(kDownloadPath, package.code, error);
  SdMan.remove(kDownloadPath);
  return installed;
}

bool LanguagePackageManager::installArchive(const std::string& archivePath, const std::string& code,
                                            std::string& error) {
  if (!SdMan.ready()) {
    error = "SD card is not ready";
    return false;
  }
  SdIoMutex::Lock ioLock;
  return installArchiveLocked(archivePath, code, error);
}

bool LanguagePackageManager::remove(const Package& package, std::string& error) {
  error.clear();
  if (!SdMan.ready() || !isSafeLanguageCode(package.code)) {
    error = SdMan.ready() ? "Invalid language package" : "SD card is not ready";
    return false;
  }

  SdIoMutex::Lock ioLock;
  const std::string systemPath = "/system/lang/" + package.code;
  const std::string fontPath = "/fonts/lang/" + package.code;
  const bool hadSystem = SdMan.exists(systemPath.c_str());
  const bool hadFonts = SdMan.exists(fontPath.c_str());
  if (!hadSystem && !hadFonts) {
    error = "Language is not installed";
    return false;
  }

  bool removed = true;
  if (hadSystem && !removeDirectoryTree(systemPath)) removed = false;
  if (hadFonts && !removeDirectoryTree(fontPath)) removed = false;
  if (!removed) error = "Could not remove language files";
  return removed;
}

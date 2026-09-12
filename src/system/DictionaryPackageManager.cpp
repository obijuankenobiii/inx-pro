#include "DictionaryPackageManager.h"

#include <SDCardManager.h>

#include <algorithm>
#include <cctype>

#include "../network/HttpDownloader.h"
#include "util/SdIoMutex.h"
#include "util/StringUtils.h"
#include <FsHelpers.h>
#include <ZipFile.h>

namespace {
constexpr char kRepositoryBase[] =
    "https://raw.githubusercontent.com/obijuankenobiii/inx-store/main/";
constexpr char kDownloadPath[] = "/.system/dictionary-package.zip";
constexpr size_t kMaxPackageBytes = 32 * 1024 * 1024;
constexpr size_t kMaxExtractedBytes = 128 * 1024 * 1024;

struct StaticPackage {
  const char* name;
  const char* folder;
  const char* path;
};

constexpr StaticPackage kStaticPackages[] = {
    {"Oxford English", "Oxford", "dictionary/oxford.zip"},
};

bool isSafeFolder(const std::string& folder) {
  if (folder.empty() || folder.size() > 48 || folder == "." || folder == "..") return false;
  for (const unsigned char c : folder) {
    if (!std::isalnum(c) && c != '_' && c != '-' && c != ' ') return false;
  }
  return true;
}

std::string entryBaseName(const std::string& entryName) {
  const size_t slash = entryName.rfind('/');
  return slash == std::string::npos ? entryName : entryName.substr(slash + 1);
}

bool isDictionaryEntry(const std::string& entryName, std::string& baseName) {
  baseName = entryBaseName(entryName);
  if (baseName.empty() || baseName == "." || baseName == "..") return false;
  if (!StringUtils::checkFileExtension(baseName, ".ifo") &&
      !StringUtils::checkFileExtension(baseName, ".idx") &&
      !StringUtils::checkFileExtension(baseName, ".dict") &&
      !StringUtils::checkFileExtension(baseName, ".syn")) {
    return false;
  }
  for (const char c : baseName) {
    if (c == '/' || c == '\\' || c == ':' || c < 32) return false;
  }
  return true;
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
  if (!SdMan.openFileForWrite("DICT", outputPath, output)) {
    error = "Could not create dictionary file on SD card";
    return false;
  }
  const bool copied = zip.readFileToStream(entryName.c_str(), output, 4096, inflatedSize);
  output.close();
  if (!copied) {
    SdMan.remove(outputPath.c_str());
    error = "Could not extract dictionary package";
    return false;
  }
  return true;
}

bool installArchiveLocked(const std::string& archivePath, const std::string& folder, std::string& error) {
  error.clear();
  if (!isSafeFolder(folder)) {
    error = "Invalid dictionary folder";
    return false;
  }

  FsFile archiveFile = SdMan.open(archivePath.c_str());
  const size_t archiveSize = archiveFile ? archiveFile.size() : 0;
  if (archiveFile) archiveFile.close();
  if (archiveSize == 0 || archiveSize > kMaxPackageBytes) {
    error = "Dictionary package is too large";
    return false;
  }

  ZipFile zip{archivePath};
  if (!zip.open() || !zip.loadAllFileStatSlims()) {
    zip.close();
    error = "Dictionary package is not a valid ZIP";
    return false;
  }

  std::vector<std::string> entries;
  size_t extractedBytes = 0;
  bool hasIfo = false;
  bool hasIdx = false;
  bool hasDict = false;
  for (size_t i = 0; i < zip.entryCount(); ++i) {
    const char* name = zip.fileNameAt(i);
    if (!name) continue;
    const std::string entryName = FsHelpers::normalisePath(name);
    if (entryName.empty() || entryName.find("__MACOSX/") == 0) continue;

    std::string baseName;
    if (!isDictionaryEntry(entryName, baseName)) continue;
    const bool isIfo = StringUtils::checkFileExtension(baseName, ".ifo");
    const bool isIdx = StringUtils::checkFileExtension(baseName, ".idx");
    const bool isDict = StringUtils::checkFileExtension(baseName, ".dict");
    if ((isIfo && hasIfo) || (isIdx && hasIdx) || (isDict && hasDict)) {
      zip.close();
      error = "Dictionary package contains duplicate index files";
      return false;
    }

    size_t inflatedSize = 0;
    if (!zip.getInflatedFileSize(entryName.c_str(), &inflatedSize) || inflatedSize == 0 ||
        extractedBytes + inflatedSize > kMaxExtractedBytes) {
      zip.close();
      error = "Dictionary package is too large after extraction";
      return false;
    }
    entries.push_back(entryName);
    extractedBytes += inflatedSize;
    hasIfo = hasIfo || isIfo;
    hasIdx = hasIdx || isIdx;
    hasDict = hasDict || isDict;
  }

  if (!hasIfo || !hasIdx || !hasDict) {
    zip.close();
    error = "Dictionary package needs .ifo, .idx, and .dict files";
    return false;
  }

  SdMan.mkdir("/dictionaries");
  const std::string targetPath = "/dictionaries/" + StringUtils::sanitizeFilename(folder, 48);
  const std::string tempPath = targetPath + ".installing";
  const std::string backupPath = targetPath + ".backup";
  if (SdMan.exists(tempPath.c_str())) removeDirectoryTree(tempPath);
  if (SdMan.exists(backupPath.c_str())) removeDirectoryTree(backupPath);
  SdMan.mkdir(tempPath.c_str());

  bool written = true;
  for (const std::string& entryName : entries) {
    std::string baseName;
    isDictionaryEntry(entryName, baseName);
    size_t inflatedSize = 0;
    if (!zip.getInflatedFileSize(entryName.c_str(), &inflatedSize) ||
        !writeZipEntry(zip, entryName, tempPath + "/" + StringUtils::sanitizeFilename(baseName, 96), inflatedSize,
                       error)) {
      written = false;
      break;
    }
  }
  zip.close();
  if (!written) {
    removeDirectoryTree(tempPath);
    return false;
  }

  if (SdMan.exists(targetPath.c_str()) && !SdMan.rename(targetPath.c_str(), backupPath.c_str())) {
    removeDirectoryTree(tempPath);
    error = "Could not replace existing dictionary";
    return false;
  }
  if (!SdMan.rename(tempPath.c_str(), targetPath.c_str())) {
    if (SdMan.exists(backupPath.c_str())) SdMan.rename(backupPath.c_str(), targetPath.c_str());
    removeDirectoryTree(tempPath);
    error = "Could not publish dictionary";
    return false;
  }
  if (SdMan.exists(backupPath.c_str())) removeDirectoryTree(backupPath);
  return true;
}
}  // namespace

bool DictionaryPackageManager::fetchAvailable(std::vector<Package>& packages, std::string& error) {
  packages.clear();
  error.clear();
  packages.reserve(sizeof(kStaticPackages) / sizeof(kStaticPackages[0]));
  for (const StaticPackage& item : kStaticPackages) {
    packages.push_back({item.name, item.folder, std::string(kRepositoryBase) + item.path, 0});
  }
  return true;
}

bool DictionaryPackageManager::isInstalled(const Package& package) {
  if (!SdMan.ready() || !isSafeFolder(package.folder)) return false;
  SdIoMutex::Lock ioLock;
  const std::string path = "/dictionaries/" + StringUtils::sanitizeFilename(package.folder, 48);
  return SdMan.exists((path + "/dictionary.ifo").c_str()) &&
         SdMan.exists((path + "/dictionary.idx").c_str()) &&
         SdMan.exists((path + "/dictionary.dict").c_str());
}

bool DictionaryPackageManager::install(const Package& package, std::string& error, ProgressCallback progress) {
  error.clear();
  if (!isSafeFolder(package.folder) || package.url.empty()) {
    error = "Invalid dictionary package";
    return false;
  }
  if (!SdMan.ready()) {
    error = "SD card is not ready";
    return false;
  }

  SdIoMutex::Lock ioLock;
  SdMan.mkdir("/.system");
  if (HttpDownloader::downloadToFile(package.url, kDownloadPath, "", "", progress) != HttpDownloader::OK) {
    error = "Dictionary download failed";
    return false;
  }
  const bool installed = installArchiveLocked(kDownloadPath, package.folder, error);
  SdMan.remove(kDownloadPath);
  return installed;
}

bool DictionaryPackageManager::remove(const Package& package, std::string& error) {
  error.clear();
  if (!SdMan.ready() || !isSafeFolder(package.folder)) {
    error = SdMan.ready() ? "Invalid dictionary package" : "SD card is not ready";
    return false;
  }
  SdIoMutex::Lock ioLock;
  const std::string path = "/dictionaries/" + StringUtils::sanitizeFilename(package.folder, 48);
  if (!SdMan.exists(path.c_str()) || !removeDirectoryTree(path)) {
    error = "Dictionary removal failed";
    return false;
  }
  return true;
}

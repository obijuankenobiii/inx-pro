#include "PluginManager.h"

#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <SDCardManager.h>
#include <ZipFile.h>

#include <cctype>
#include <cstdlib>

#include "../network/HttpDownloader.h"
#include "util/SdIoMutex.h"

namespace {
constexpr char kRepositoryBase[] =
    "https://raw.githubusercontent.com/obijuankenobiii/inx-store/main/";
constexpr char kDownloadPath[] = "/.system/plugin-package.zip";
constexpr char kPluginRoot[] = "/.system/plugins";
constexpr size_t kMaxPackageBytes = 512 * 1024;
constexpr size_t kMaxManifestBytes = 16 * 1024;

struct StaticPackage {
  const char* id;
  const char* name;
  const char* description;
  const char* path;
};

constexpr StaticPackage kStaticPackages[] = {
    {"study-cards", "Study Cards", "Save selected passages and export them for Anki", "plugin/study-cards.zip"},
};

bool safeId(const std::string& id) {
  if (id.empty() || id.size() > 48 || id == "." || id == "..") return false;
  for (const unsigned char c : id) {
    if (!std::isalnum(c) && c != '-' && c != '_') return false;
  }
  return true;
}

std::string pluginPath(const std::string& id) { return std::string(kPluginRoot) + "/" + id; }

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
    const std::string child = path + "/" + name;
    if (isDirectory) {
      if (!removeDirectoryTree(child)) removed = false;
    } else if (!SdMan.remove(child.c_str())) {
      removed = false;
    }
  }
  directory.close();
  return removed && SdMan.removeDir(path.c_str());
}

bool readManifest(ZipFile& zip, const std::string& expectedId, std::string& manifest, std::string& error) {
  size_t size = 0;
  if (!zip.getInflatedFileSize("manifest.json", &size) || size == 0 || size > kMaxManifestBytes) {
    error = "Plugin manifest is missing or too large";
    return false;
  }
  size_t actual = 0;
  uint8_t* bytes = zip.readFileToMemory("manifest.json", &actual, true);
  if (!bytes || actual == 0 || actual > kMaxManifestBytes) {
    if (bytes) free(bytes);
    error = "Could not read plugin manifest";
    return false;
  }
  manifest.assign(reinterpret_cast<const char*>(bytes), actual);
  free(bytes);

  JsonDocument doc;
  if (deserializeJson(doc, manifest)) {
    error = "Plugin manifest is invalid";
    return false;
  }
  const std::string id = doc["id"] | "";
  if (id != expectedId || !safeId(id)) {
    error = "Plugin manifest ID does not match the package";
    return false;
  }
  return true;
}

bool installArchiveLocked(const std::string& archivePath, const std::string& id, std::string& error) {
  if (!safeId(id)) {
    error = "Invalid plugin ID";
    return false;
  }
  FsFile archive = SdMan.open(archivePath.c_str());
  const size_t size = archive ? archive.size() : 0;
  if (archive) archive.close();
  if (size == 0 || size > kMaxPackageBytes) {
    error = "Plugin package is too large";
    return false;
  }

  ZipFile zip{archivePath};
  if (!zip.open() || !zip.loadAllFileStatSlims()) {
    zip.close();
    error = "Plugin package is not a valid ZIP";
    return false;
  }
  std::string manifest;
  if (!readManifest(zip, id, manifest, error)) {
    zip.close();
    return false;
  }
  zip.close();

  SdMan.mkdir("/.system");
  SdMan.mkdir(kPluginRoot);
  const std::string target = pluginPath(id);
  SdMan.mkdir(target.c_str());
  FsFile output;
  const std::string manifestPath = target + "/manifest.json";
  if (!SdMan.openFileForWrite("PLUGIN", manifestPath, output)) {
    error = "Could not install plugin manifest";
    return false;
  }
  if (output.write(reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size()) != manifest.size()) {
    output.close();
    SdMan.remove(manifestPath.c_str());
    error = "Could not write plugin manifest";
    return false;
  }
  output.close();
  return true;
}
}  // namespace

bool PluginManager::fetchAvailable(std::vector<Package>& packages, std::string& error) {
  packages.clear();
  error.clear();
  for (const StaticPackage& item : kStaticPackages) {
    packages.push_back({item.id, item.name, item.description, std::string(kRepositoryBase) + item.path, 0});
  }
  return true;
}

bool PluginManager::isInstalled(const char* id) {
  if (!id || !safeId(id) || !SdMan.ready()) return false;
  SdIoMutex::Lock ioLock;
  return SdMan.exists((pluginPath(id) + "/manifest.json").c_str());
}

bool PluginManager::isInstalled(const Package& package) { return isInstalled(package.id.c_str()); }

bool PluginManager::install(const Package& package, std::string& error, ProgressCallback progress) {
  error.clear();
  if (!safeId(package.id) || package.url.empty() || !SdMan.ready()) {
    error = !SdMan.ready() ? "SD card is not ready" : "Invalid plugin package";
    return false;
  }
  SdIoMutex::Lock ioLock;
  SdMan.mkdir("/.system");
  if (HttpDownloader::downloadToFile(package.url, kDownloadPath, "", "", progress) != HttpDownloader::OK) {
    error = "Plugin download failed";
    SdMan.remove(kDownloadPath);
    return false;
  }
  const bool installed = installArchiveLocked(kDownloadPath, package.id, error);
  SdMan.remove(kDownloadPath);
  return installed;
}

bool PluginManager::installArchive(const std::string& archivePath, const std::string& id, std::string& error) {
  if (!SdMan.ready()) {
    error = "SD card is not ready";
    return false;
  }
  SdIoMutex::Lock ioLock;
  return installArchiveLocked(archivePath, id, error);
}

bool PluginManager::remove(const Package& package, std::string& error) {
  error.clear();
  if (!safeId(package.id) || !SdMan.ready()) {
    error = !SdMan.ready() ? "SD card is not ready" : "Invalid plugin package";
    return false;
  }
  SdIoMutex::Lock ioLock;
  const std::string manifestPath = pluginPath(package.id) + "/manifest.json";
  if (!SdMan.exists(manifestPath.c_str()) || !SdMan.remove(manifestPath.c_str())) {
    error = "Plugin removal failed";
    return false;
  }
  return true;
}

#include "PluginManager.h"

#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <SDCardManager.h>
#include <ZipFile.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "esp_heap_caps.h"

#include "../network/HttpDownloader.h"
#include "util/SdIoMutex.h"

namespace {
constexpr char kRepositoryBase[] =
    "https://raw.githubusercontent.com/obijuankenobiii/inx-store/main/";
constexpr char kDownloadPath[] = "/.system/plugin-package.zip";
constexpr char kPluginRoot[] = "/.system/plugins";
constexpr size_t kMaxPackageBytes = 512 * 1024;
constexpr size_t kMaxManifestBytes = 16 * 1024;
constexpr size_t kMaxScriptBytes = 128 * 1024;
constexpr size_t kMaxExtractedBytes = 256 * 1024;
constexpr char kPluginIdRegistryKey[] = "inx.plugin.id";

struct StaticPackage {
  const char* id;
  const char* name;
  const char* description;
  const char* path;
};

constexpr StaticPackage kStaticPackages[] = {
    {"study-cards", "Anki Export", "Adds anki supported export", "plugin/study-cards.zip"},
};

bool safeId(const std::string& id) {
  if (id.empty() || id.size() > 48 || id == "." || id == "..") return false;
  for (const unsigned char c : id) {
    if (!std::isalnum(c) && c != '-' && c != '_') return false;
  }
  return true;
}

std::string pluginPath(const std::string& id) { return std::string(kPluginRoot) + "/" + id; }

struct LoadedPlugin {
  std::string id;
  lua_State* state = nullptr;
};

std::vector<LoadedPlugin> gLoadedPlugins;

// Lua tables, VM stacks, strings, and compiled chunks are ordinary byte
// buffers and do not need internal RAM. Keep the Lua heap in PSRAM so loading
// a plugin does not take tens of kilobytes from the reader's working heap.
void* luaPsramAllocator(void*, void* pointer, const size_t oldSize, const size_t newSize) {
  if (newSize == 0) {
    if (pointer) heap_caps_free(pointer);
    return nullptr;
  }

  void* replacement = heap_caps_malloc(newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!replacement) return nullptr;
  if (pointer) {
    std::memcpy(replacement, pointer, std::min(oldSize, newSize));
    heap_caps_free(pointer);
  }
  return replacement;
}

size_t internalHeapFree() { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }
size_t psramHeapFree() { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

bool safeEntryName(const std::string& name) {
  if (name.empty() || name.size() > 96 || name == "." || name == ".." || name.find('/') != std::string::npos ||
      name.find('\\') != std::string::npos || name.find("..") != std::string::npos) {
    return false;
  }
  for (const unsigned char c : name) {
    if (c < 32) return false;
  }
  return true;
}

bool readSdFile(const std::string& path, std::string& contents, const size_t maxBytes) {
  FsFile file = SdMan.open(path.c_str(), O_READ);
  if (!file) return false;
  const size_t size = file.size();
  if (size == 0 || size > maxBytes) {
    file.close();
    return false;
  }
  contents.resize(size);
  const size_t read = file.read(reinterpret_cast<uint8_t*>(contents.data()), size);
  file.close();
  if (read != size) {
    contents.clear();
    return false;
  }
  return true;
}

bool readInstalledManifest(const std::string& id, JsonDocument& document) {
  std::string manifest;
  if (!readSdFile(pluginPath(id) + "/manifest.json", manifest, kMaxManifestBytes)) return false;
  return deserializeJson(document, manifest) == DeserializationError::Ok;
}

bool safeWebPath(const std::string& path) {
  if (path.empty() || path.size() > 64 || path[0] != '/' || path.find("..") != std::string::npos) return false;
  for (const unsigned char c : path) {
    if (!std::isalnum(c) && c != '/' && c != '-' && c != '_' && c != '.') return false;
  }
  return true;
}

bool readWebLink(const char* candidate, PluginManager::WebLink& link) {
  JsonDocument document;
  if (!readInstalledManifest(candidate, document)) return false;

  const JsonObject web = document["web"].as<JsonObject>();
  const char* page = web["page"] | "";
  const char* script = web["script"] | "";
  if (!page || !script || !safeEntryName(page) || !safeEntryName(script)) return false;

  const std::string id = candidate ? candidate : "";
  const char* name = document["name"] | "";
  const char* label = web["label"] | "";
  const char* configuredPath = web["path"] | "";
  const char* icon = web["icon"] | "";
  if (icon && icon[0] && !safeEntryName(icon)) return false;
  const std::string path = configuredPath && configuredPath[0] ? configuredPath : "/plugin/" + id;
  if (!safeWebPath(path)) return false;

  link.id = id;
  link.name = name ? name : id;
  link.label = label && label[0] ? label : link.name;
  link.path = path;
  link.page = page;
  link.script = script;
  link.icon = icon ? icon : "";
  link.order = web["order"] | 100;
  return true;
}

template <typename Callback>
bool forEachInstalledPlugin(const Callback& callback) {
  FsFile directory = SdMan.open(kPluginRoot);
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    return false;
  }
  char name[96] = {};
  while (true) {
    FsFile entry = directory.openNextFile();
    if (!entry) break;
    entry.getName(name, sizeof(name));
    const bool isDirectory = entry.isDirectory();
    entry.close();
    if (isDirectory && safeId(name) && std::strcmp(name, "series") != 0 && callback(name)) {
      directory.close();
      return true;
    }
  }
  directory.close();
  return false;
}

void closeLoadedPlugin(const char* id) {
  if (!id) return;
  for (auto it = gLoadedPlugins.begin(); it != gLoadedPlugins.end(); ++it) {
    if (it->id == id) {
      if (it->state) lua_close(it->state);
      gLoadedPlugins.erase(it);
      return;
    }
  }
}

bool luaValueToJson(JsonVariant target, lua_State* state, const int index, const int depth) {
  if (depth > 4) return false;
  const int type = lua_type(state, index);
  switch (type) {
    case LUA_TSTRING:
      target.set(lua_tostring(state, index));
      return true;
    case LUA_TBOOLEAN:
      target.set(lua_toboolean(state, index) != 0);
      return true;
    case LUA_TNUMBER:
      if (lua_isinteger(state, index)) {
        target.set(static_cast<int64_t>(lua_tointeger(state, index)));
      } else {
        target.set(static_cast<double>(lua_tonumber(state, index)));
      }
      return true;
    case LUA_TTABLE: {
      JsonObject object = target.to<JsonObject>();
      const int absoluteIndex = lua_absindex(state, index);
      lua_pushnil(state);
      while (lua_next(state, absoluteIndex) != 0) {
        if (!lua_isstring(state, -2)) {
          lua_pop(state, 2);
          return false;
        }
        const char* key = lua_tostring(state, -2);
        if (!key || !luaValueToJson(object[key], state, -1, depth + 1)) {
          lua_pop(state, 2);
          return false;
        }
        lua_pop(state, 1);
      }
      return true;
    }
    default:
      return false;
  }
}

int luaStorageAppendJson(lua_State* state) {
  const char* filename = luaL_checkstring(state, 1);
  luaL_checktype(state, 2, LUA_TTABLE);
  const std::string name(filename ? filename : "");
  if (!safeEntryName(name) || name.size() > 64) return luaL_error(state, "invalid storage filename");

  lua_getfield(state, LUA_REGISTRYINDEX, kPluginIdRegistryKey);
  const char* pluginId = lua_tostring(state, -1);
  if (!pluginId || !safeId(pluginId)) {
    lua_pop(state, 1);
    return luaL_error(state, "plugin identity is unavailable");
  }
  const std::string path = pluginPath(pluginId) + "/" + name;
  lua_pop(state, 1);

  JsonDocument document;
  if (!luaValueToJson(document.to<JsonObject>(), state, 2, 0)) return luaL_error(state, "unsupported card value");

  SdIoMutex::Lock ioLock;
  FsFile file = SdMan.open(path.c_str(), O_WRITE | O_CREAT | O_APPEND);
  if (!file) {
    lua_pushboolean(state, 0);
    return 1;
  }
  const size_t written = serializeJson(document, file);
  file.write(static_cast<uint8_t>('\n'));
  file.close();
  lua_pushboolean(state, written > 0);
  return 1;
}

int luaStorageAppendText(lua_State* state) {
  const char* filename = luaL_checkstring(state, 1);
  size_t length = 0;
  const char* text = luaL_checklstring(state, 2, &length);
  const std::string name(filename ? filename : "");
  if (!safeEntryName(name) || name.size() > 64) return luaL_error(state, "invalid storage filename");

  lua_getfield(state, LUA_REGISTRYINDEX, kPluginIdRegistryKey);
  const char* pluginId = lua_tostring(state, -1);
  if (!pluginId || !safeId(pluginId)) {
    lua_pop(state, 1);
    return luaL_error(state, "plugin identity is unavailable");
  }
  const std::string path = pluginPath(pluginId) + "/" + name;
  lua_pop(state, 1);

  SdIoMutex::Lock ioLock;
  FsFile file = SdMan.open(path.c_str(), O_WRITE | O_CREAT | O_APPEND);
  if (!file) {
    lua_pushboolean(state, 0);
    return 1;
  }
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(text), length);
  file.close();
  lua_pushboolean(state, written == length);
  return 1;
}

int luaStorageWriteText(lua_State* state) {
  const char* filename = luaL_checkstring(state, 1);
  size_t length = 0;
  const char* text = luaL_checklstring(state, 2, &length);
  const std::string name(filename ? filename : "");
  if (!safeEntryName(name) || name.size() > 64) return luaL_error(state, "invalid storage filename");

  lua_getfield(state, LUA_REGISTRYINDEX, kPluginIdRegistryKey);
  const char* pluginId = lua_tostring(state, -1);
  if (!pluginId || !safeId(pluginId)) {
    lua_pop(state, 1);
    return luaL_error(state, "plugin identity is unavailable");
  }
  const std::string path = pluginPath(pluginId) + "/" + name;
  lua_pop(state, 1);

  SdIoMutex::Lock ioLock;
  FsFile file = SdMan.open(path.c_str(), O_WRITE | O_CREAT | O_TRUNC);
  if (!file) {
    lua_pushboolean(state, 0);
    return 1;
  }
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(text), length);
  file.close();
  lua_pushboolean(state, written == length);
  return 1;
}

int luaStorageReadAll(lua_State* state) {
  const char* filename = luaL_checkstring(state, 1);
  const std::string name(filename ? filename : "");
  if (!safeEntryName(name) || name.size() > 64) return luaL_error(state, "invalid storage filename");

  lua_getfield(state, LUA_REGISTRYINDEX, kPluginIdRegistryKey);
  const char* pluginId = lua_tostring(state, -1);
  if (!pluginId || !safeId(pluginId)) {
    lua_pop(state, 1);
    return luaL_error(state, "plugin identity is unavailable");
  }
  const std::string path = pluginPath(pluginId) + "/" + name;
  lua_pop(state, 1);

  std::string contents;
  if (!readSdFile(path, contents, 256 * 1024)) {
    lua_pushliteral(state, "");
    return 1;
  }
  lua_pushlstring(state, contents.c_str(), contents.size());
  return 1;
}

int luaLog(lua_State* state) {
  const int count = lua_gettop(state);
  INX_SERIAL.printf("[LUA] ");
  for (int i = 1; i <= count; ++i) {
    if (i > 1) INX_SERIAL.printf(" ");
    const char* value = luaL_tolstring(state, i, nullptr);
    INX_SERIAL.printf("%s", value ? value : "(nil)");
    lua_pop(state, 1);
  }
  INX_SERIAL.printf("\n");
  return 0;
}

void registerLuaApi(lua_State* state, const std::string& id) {
  lua_pushlstring(state, id.c_str(), id.size());
  lua_setfield(state, LUA_REGISTRYINDEX, kPluginIdRegistryKey);

  lua_newtable(state);  // inx
  lua_newtable(state);  // inx.storage
  lua_pushcfunction(state, luaStorageAppendJson);
  lua_setfield(state, -2, "append_json");
  lua_pushcfunction(state, luaStorageAppendText);
  lua_setfield(state, -2, "append_text");
  lua_pushcfunction(state, luaStorageWriteText);
  lua_setfield(state, -2, "write_text");
  lua_pushcfunction(state, luaStorageReadAll);
  lua_setfield(state, -2, "read_all");
  lua_setfield(state, -2, "storage");
  lua_pushcfunction(state, luaLog);
  lua_setfield(state, -2, "log");
  lua_setglobal(state, "inx");
}

void openSafeLuaLibraries(lua_State* state) {
  luaL_requiref(state, LUA_GNAME, luaopen_base, 1);
  lua_pop(state, 1);
  lua_pushnil(state);
  lua_setglobal(state, "dofile");
  lua_pushnil(state);
  lua_setglobal(state, "loadfile");
  luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop(state, 1);
  luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1);
  lua_pop(state, 1);
  luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1);
  lua_pop(state, 1);
  luaL_requiref(state, LUA_UTF8LIBNAME, luaopen_utf8, 1);
  lua_pop(state, 1);
  luaL_requiref(state, LUA_COLIBNAME, luaopen_coroutine, 1);
  lua_pop(state, 1);
}

lua_State* loadPlugin(const std::string& id, std::string& error) {
  for (const LoadedPlugin& loaded : gLoadedPlugins) {
    if (loaded.id == id) return loaded.state;
  }

  std::string source;
  const std::string path = pluginPath(id) + "/main.lua";
  if (!readSdFile(path, source, kMaxScriptBytes)) {
    error = "Lua entrypoint main.lua is missing or too large";
    return nullptr;
  }
  const size_t internalBefore = internalHeapFree();
  const size_t psramBefore = psramHeapFree();
  lua_State* state = lua_newstate(luaPsramAllocator, nullptr);
  if (!state) {
    error = "Could not create Lua state";
    return nullptr;
  }
  openSafeLuaLibraries(state);
  registerLuaApi(state, id);
  if (luaL_loadbuffer(state, source.data(), source.size(), path.c_str()) != LUA_OK ||
      lua_pcall(state, 0, 0, 0) != LUA_OK) {
    const char* message = lua_tostring(state, -1);
    error = message ? message : "Lua plugin failed to load";
    lua_close(state);
    return nullptr;
  }
  gLoadedPlugins.push_back({id, state});
  INX_SERIAL.printf("[LUA] loaded plugin=%s internal_free=%u (%d) psram_free=%u (%d)\n", id.c_str(),
                    static_cast<unsigned>(internalHeapFree()),
                    static_cast<int>(internalHeapFree()) - static_cast<int>(internalBefore),
                    static_cast<unsigned>(psramHeapFree()),
                    static_cast<int>(psramHeapFree()) - static_cast<int>(psramBefore));
  return state;
}

bool writeZipEntry(ZipFile& zip, const std::string& entryName, const std::string& outputPath,
                   const size_t inflatedSize, std::string& error) {
  FsFile output;
  if (!SdMan.openFileForWrite("PLUGIN", outputPath, output)) {
    error = "Could not create plugin file on SD card";
    return false;
  }
  const bool copied = zip.readFileToStream(entryName.c_str(), output, 4096, inflatedSize);
  output.close();
  if (!copied) {
    SdMan.remove(outputPath.c_str());
    error = "Could not extract plugin package";
    return false;
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

  std::vector<std::string> entries;
  size_t extractedBytes = 0;
  bool hasEntrypoint = false;
  for (size_t i = 0; i < zip.entryCount(); ++i) {
    const char* rawName = zip.fileNameAt(i);
    if (!rawName) continue;
    const std::string entryName = FsHelpers::normalisePath(rawName);
    if (entryName.empty() || entryName.back() == '/' || entryName.find("__MACOSX/") == 0) continue;
    if (!safeEntryName(entryName)) {
      zip.close();
      error = "Plugin package contains an unsafe file name";
      return false;
    }
    size_t inflatedSize = 0;
    if (!zip.getInflatedFileSize(entryName.c_str(), &inflatedSize) || inflatedSize == 0 ||
        extractedBytes + inflatedSize > kMaxExtractedBytes) {
      zip.close();
      error = "Plugin package is too large after extraction";
      return false;
    }
    entries.push_back(entryName);
    extractedBytes += inflatedSize;
    hasEntrypoint = hasEntrypoint || entryName == "main.lua";
  }
  if (!hasEntrypoint) {
    zip.close();
    error = "Plugin package must contain main.lua";
    return false;
  }

  SdMan.mkdir("/.system");
  SdMan.mkdir(kPluginRoot);
  const std::string target = pluginPath(id);
  closeLoadedPlugin(id.c_str());
  if (SdMan.exists(target.c_str()) && !removeDirectoryTree(target)) {
    zip.close();
    error = "Could not replace existing plugin";
    return false;
  }
  SdMan.mkdir(target.c_str());
  for (const std::string& entryName : entries) {
    size_t inflatedSize = 0;
    if (!zip.getInflatedFileSize(entryName.c_str(), &inflatedSize) ||
        !writeZipEntry(zip, entryName, target + "/" + entryName, inflatedSize, error)) {
      zip.close();
      removeDirectoryTree(target);
      return false;
    }
  }
  zip.close();
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
  return SdMan.exists((pluginPath(id) + "/manifest.json").c_str()) &&
         SdMan.exists((pluginPath(id) + "/main.lua").c_str());
}

bool PluginManager::isInstalled(const Package& package) { return isInstalled(package.id.c_str()); }

bool PluginManager::isLuaPlugin(const char* id) { return isInstalled(id); }

bool PluginManager::invoke(const char* id, const char* function, const LuaArgumentPusher& pushArguments,
                           std::string& error) {
  error.clear();
  if (!id || !function || !safeId(id) || !SdMan.ready()) {
    error = "Invalid Lua plugin request";
    return false;
  }
  lua_State* state = loadPlugin(id, error);
  if (!state) return false;

  lua_getglobal(state, function);
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 1);
    error = std::string("Lua function is missing: ") + function;
    return false;
  }
  const int argumentBase = lua_gettop(state);
  if (pushArguments) pushArguments(state);
  const int argumentCount = lua_gettop(state) - argumentBase;
  if (lua_pcall(state, argumentCount, 1, 0) != LUA_OK) {
    const char* message = lua_tostring(state, -1);
    error = message ? message : "Lua plugin call failed";
    lua_pop(state, 1);
    INX_SERIAL.printf("[LUA] plugin=%s function=%s error=%s\n", id, function, error.c_str());
    return false;
  }
  const bool result = lua_toboolean(state, -1) != 0;
  lua_pop(state, 1);
  if (!result) error = "Lua plugin returned false";
  return result;
}

bool PluginManager::invokeString(const char* id, const char* function, const LuaArgumentPusher& pushArguments,
                                 std::string& result, std::string& error) {
  result.clear();
  error.clear();
  if (!id || !function || !safeId(id) || !SdMan.ready()) {
    error = "Invalid Lua plugin request";
    return false;
  }
  lua_State* state = loadPlugin(id, error);
  if (!state) return false;

  lua_getglobal(state, function);
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 1);
    error = std::string("Lua function is missing: ") + function;
    return false;
  }
  const int argumentBase = lua_gettop(state);
  if (pushArguments) pushArguments(state);
  const int argumentCount = lua_gettop(state) - argumentBase;
  if (lua_pcall(state, argumentCount, 1, 0) != LUA_OK) {
    const char* message = lua_tostring(state, -1);
    error = message ? message : "Lua plugin call failed";
    lua_pop(state, 1);
    INX_SERIAL.printf("[LUA] plugin=%s function=%s error=%s\n", id, function, error.c_str());
    return false;
  }
  if (!lua_isstring(state, -1)) {
    lua_pop(state, 1);
    error = "Lua plugin did not return text";
    return false;
  }
  result = lua_tostring(state, -1);
  lua_pop(state, 1);
  return true;
}

namespace {

bool pushJsonValue(lua_State* state, JsonVariantConst value, const int depth) {
  if (depth > 6) return false;
  if (value.isNull()) {
    lua_pushnil(state);
    return true;
  }
  if (value.is<bool>()) {
    lua_pushboolean(state, value.as<bool>() ? 1 : 0);
    return true;
  }
  if (value.is<long>() || value.is<int>() || value.is<int64_t>()) {
    lua_pushinteger(state, static_cast<lua_Integer>(value.as<int64_t>()));
    return true;
  }
  if (value.is<float>() || value.is<double>()) {
    lua_pushnumber(state, static_cast<lua_Number>(value.as<double>()));
    return true;
  }
  if (value.is<const char*>()) {
    const char* text = value.as<const char*>();
    lua_pushstring(state, text ? text : "");
    return true;
  }
  if (value.is<JsonArrayConst>()) {
    lua_newtable(state);
    int index = 1;
    for (JsonVariantConst item : value.as<JsonArrayConst>()) {
      if (!pushJsonValue(state, item, depth + 1)) return false;
      lua_rawseti(state, -2, index++);
    }
    return true;
  }
  if (value.is<JsonObjectConst>()) {
    lua_newtable(state);
    for (JsonPairConst pair : value.as<JsonObjectConst>()) {
      if (!pushJsonValue(state, pair.value(), depth + 1)) return false;
      lua_setfield(state, -2, pair.key().c_str());
    }
    return true;
  }
  return false;
}

}  // namespace

bool PluginManager::invokeStringJson(const char* id, const char* function, const std::string& jsonArguments,
                                     std::string& result, std::string& error) {
  JsonDocument document;
  if (jsonArguments.empty() || deserializeJson(document, jsonArguments) != DeserializationError::Ok ||
      !document.is<JsonObject>()) {
    error = "Plugin arguments must be a JSON object";
    return false;
  }
  return invokeString(id, function,
                      [&document](lua_State* state) { pushJsonValue(state, document.as<JsonObjectConst>(), 0); },
                      result, error);
}

bool PluginManager::readFile(const char* id, const char* filename, std::string& contents, const size_t maxBytes,
                             std::string& error) {
  contents.clear();
  error.clear();
  if (!id || !filename || !safeId(id)) {
    error = "Invalid plugin file request";
    return false;
  }
  const std::string name = FsHelpers::normalisePath(filename);
  if (!safeEntryName(name)) {
    error = "Invalid plugin file name";
    return false;
  }
  if (!SdMan.ready() || !readSdFile(pluginPath(id) + "/" + name, contents, maxBytes)) {
    error = "Plugin file is missing or too large";
    return false;
  }
  return true;
}

bool PluginManager::findReaderSelectionPlugin(std::string& id, std::string& label, std::string& function) {
  id.clear();
  label.clear();
  function.clear();
  if (!SdMan.ready()) return false;
  SdIoMutex::Lock ioLock;
  return forEachInstalledPlugin([&](const char* candidate) {
    JsonDocument document;
    if (!readInstalledManifest(candidate, document)) return false;
    const JsonObject action = document["reader_action"].as<JsonObject>();
    const std::string candidateLabel = action["label"] | "";
    const std::string candidateFunction = action["function"] | "";
    if (candidateLabel.empty() || candidateFunction.empty()) return false;
    id = candidate;
    label = candidateLabel;
    function = candidateFunction;
    return true;
  });
}

bool PluginManager::findReaderSuggestionPlugin(ReaderSuggestionLink& link) {
  link = {};
  if (!SdMan.ready()) return false;
  SdIoMutex::Lock ioLock;
  return forEachInstalledPlugin([&](const char* candidate) {
    JsonDocument document;
    if (!readInstalledManifest(candidate, document)) return false;
    const JsonObject hook = document["reader_suggestion"].as<JsonObject>();
    const std::string candidateFunction = hook["function"] | "";
    if (candidateFunction.empty()) return false;
    link.id = candidate;
    link.label = hook["label"] | "Open suggested book";
    link.function = candidateFunction;
    link.groupField = hook["group_field"] | "group";
    return true;
  });
}

bool PluginManager::findLibraryMenuPlugin(LibraryMenuLink& link) {
  link = {};
  if (!SdMan.ready()) return false;
  SdIoMutex::Lock ioLock;
  return forEachInstalledPlugin([&](const char* candidate) {
    JsonDocument document;
    if (!readInstalledManifest(candidate, document)) return false;
    const JsonObject menu = document["library_menu"].as<JsonObject>();
    const std::string label = menu["label"] | "";
    const std::string function = menu["function"] | "";
    if (label.empty() || function.empty()) return false;
    link.id = candidate;
    link.label = label;
    link.function = function;
    link.groupField = menu["group_field"] | "group";
    link.orderField = menu["order_field"] | "order";
    return true;
  });
}

bool PluginManager::listWebPlugins(std::vector<WebLink>& links) {
  links.clear();
  if (!SdMan.ready()) return false;
  SdIoMutex::Lock ioLock;
  forEachInstalledPlugin([&](const char* candidate) {
    WebLink link;
    if (readWebLink(candidate, link)) links.push_back(link);
    return false;
  });
  std::sort(links.begin(), links.end(), [](const WebLink& left, const WebLink& right) {
    if (left.order != right.order) return left.order < right.order;
    return left.label < right.label;
  });
  return !links.empty();
}

bool PluginManager::findWebPlugin(std::string& id, std::string& page, std::string& script) {
  id.clear();
  page.clear();
  script.clear();
  std::vector<WebLink> links;
  if (!listWebPlugins(links)) return false;
  id = links.front().id;
  page = links.front().page;
  script = links.front().script;
  return true;
}

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
  closeLoadedPlugin(package.id.c_str());
  const std::string target = pluginPath(package.id);
  if (!SdMan.exists(target.c_str()) || !removeDirectoryTree(target)) {
    error = "Plugin removal failed";
    return false;
  }
  return true;
}

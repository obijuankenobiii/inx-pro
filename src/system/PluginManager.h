#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

struct lua_State;

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

  struct WebLink {
    std::string id;
    std::string name;
    std::string label;
    std::string path;
    std::string page;
    std::string script;
    std::string icon;
    int order = 0;
  };

  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;
  using LuaArgumentPusher = std::function<void(lua_State* state)>;

  static bool fetchAvailable(std::vector<Package>& packages, std::string& error);
  static bool install(const Package& package, std::string& error, ProgressCallback progress = nullptr);
  static bool installArchive(const std::string& archivePath, const std::string& id, std::string& error);
  static bool remove(const Package& package, std::string& error);
  static bool isInstalled(const Package& package);
  static bool isInstalled(const char* id);

  /** Returns true only for an installed plugin containing a Lua entrypoint. */
  static bool isLuaPlugin(const char* id);

  /**
   * Invoke a Lua function exported by an installed plugin.
   * The argument pusher must leave exactly the function's arguments on the Lua stack.
   * A plugin call succeeds only when the Lua function returns true.
   */
  static bool invoke(const char* id, const char* function, const LuaArgumentPusher& pushArguments,
                     std::string& error);
  static bool invokeString(const char* id, const char* function, const LuaArgumentPusher& pushArguments,
                           std::string& result, std::string& error);

  /** Read a file shipped inside an installed plugin package. */
  static bool readFile(const char* id, const char* filename, std::string& contents, size_t maxBytes,
                       std::string& error);

  /** Find an installed plugin that contributes a reader-selection action. */
  static bool findReaderSelectionPlugin(std::string& id, std::string& label, std::string& function);

  /** Find an installed plugin that contributes a web page and script. */
  static bool findWebPlugin(std::string& id, std::string& page, std::string& script);

  /** List all installed plugins that contribute a web page and navigation link. */
  static bool listWebPlugins(std::vector<WebLink>& links);

 private:
  PluginManager() = delete;
};

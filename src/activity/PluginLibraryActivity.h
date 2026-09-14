#pragma once

#include "Activity.h"

#include <functional>
#include <string>
#include <vector>

class PluginLibraryActivity final : public Activity {
 public:
  PluginLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string pluginId,
                        std::string function, std::string title, std::function<void()> onBack);

  void onEnter() override;
  void loop() override;

 private:
  struct Book {
    std::string path;
    std::string title;
    std::string author;
    std::string series;
    int order = 0;
  };

  std::string pluginId_;
  std::string function_;
  std::string title_;
  std::function<void()> onBack_;
  std::vector<Book> books_;
  std::vector<std::string> visiblePaths_;
  int scroll_ = 0;
  bool error_ = false;
  std::string errorMessage_;

  void load();
  void render();
  void openBook(const std::string& path);
};

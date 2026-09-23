#pragma once

#include <array>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "activity/Activity.h"
#include "activity/page/components/widget/Library.h"

/** Settings screen for the Library home widget. */
class BaseLibraryActivity final : public Activity {
 public:
  using FolderPaths = LibraryWidget::FolderPaths;
  using ApplyCallback = std::function<void(const FolderPaths&, bool, bool)>;

  BaseLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, FolderPaths folders, bool background,
                      bool showLabel, ApplyCallback onApply, std::function<void()> onBack)
      : Activity("BaseLibrary", renderer, mappedInput), selectedFolders_(std::move(folders)), background_(background),
        showLabel_(showLabel), onApply_(std::move(onApply)), onBack_(std::move(onBack)) {}

  void onEnter() override;
  void loop() override;

 private:
  static constexpr int kRowHeight = 70;

  FolderPaths selectedFolders_;
  bool background_ = false;
  bool showLabel_ = true;
  bool folderPopup_ = false;
  int folderPopupIndex_ = 0;
  int folderPopupScroll_ = 0;
  std::array<int, LibraryWidget::kFolderCount> folderSelected_{};
  std::vector<LibraryWidget::Folder> availableFolders_;
  ApplyCallback onApply_;
  std::function<void()> onBack_;

  void render();
  void renderFolderPopup();
  void close();
  void handleTouch(int x, int y);
};

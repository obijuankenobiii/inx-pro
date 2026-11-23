#pragma once

#include <functional>
#include <utility>

#include "activity/ActivityWithSubactivity.h"
#include "activity/settings/SettingsListLayout.h"

class SleepWallpaperActivity final : public ActivityWithSubactivity {
 public:
  SleepWallpaperActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onBack)
      : SleepWallpaperActivity(renderer, mappedInput, std::move(onBack), false) {}

  SleepWallpaperActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onBack,
                         bool bookCover)
      : ActivityWithSubactivity("SleepWallpaper", renderer, mappedInput), onBack_(std::move(onBack)),
        bookCover_(bookCover) {}

  void onEnter() override;
  void loop() override;

 private:
  static constexpr int kRowHeight = SettingsListLayout::kRowHeight;

  std::function<void()> onBack_;
  bool bookCover_ = false;
  bool transparent_ = false;
  bool qualityPopup_ = false;
  bool coverModePopup_ = false;
  int contentTop_ = 0;

  void render();
  void renderQualityPopup();
  void renderCoverModePopup();
  void openImagePicker();
  void close();
  void handleTouch(int x, int y);
};

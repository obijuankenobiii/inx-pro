#pragma once

#include <functional>
#include <utility>

#include "activity/ActivityWithSubactivity.h"

/** Store entry page opened from the Home shortcut drawer. */
class StoreActivity final : public ActivityWithSubactivity {
 public:
  StoreActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onBack)
      : ActivityWithSubactivity("Store", renderer, mappedInput), onBack_(std::move(onBack)) {}

  void onEnter() override;
  void loop() override;

 private:
  static constexpr int kRowHeight = 100;
  static constexpr int kListPadding = 30;
  static constexpr int kMenuItemCount = 4;

  std::function<void()> onBack_;
  int selectedIndex_ = 0;
  bool subActivityFinished_ = false;

  static int bodyTop();
  void openSelected();
  void render();
};

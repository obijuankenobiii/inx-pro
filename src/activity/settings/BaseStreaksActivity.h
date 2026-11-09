#pragma once

#include <functional>

#include "activity/Activity.h"
#include "SettingsListLayout.h"

/** Settings screen for the Streaks home widget. */
class BaseStreaksActivity final : public Activity {
 public:
  using ApplyCallback = std::function<void(bool)>;

  BaseStreaksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool compact,
                      ApplyCallback onApply, std::function<void()> onBack)
      : Activity("BaseStreaks", renderer, mappedInput), compact_(compact), onApply_(std::move(onApply)),
        onBack_(std::move(onBack)) {}

  void onEnter() override;
  void loop() override;

 private:
  static constexpr int kRowHeight = SettingsListLayout::kRowHeight;

  bool compact_ = false;
  int contentTop_ = 0;
  ApplyCallback onApply_;
  std::function<void()> onBack_;

  void render();
  void close();
  void handleTouch(int x, int y);
};

#pragma once

#include <functional>

#include "activity/Activity.h"

/** Settings screen for the Description home widget. */
class BaseDescriptionActivity final : public Activity {
 public:
  using ApplyCallback = std::function<void(bool, bool, bool, bool)>;

  BaseDescriptionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool background, bool showTitle,
                          bool showAuthor, bool showProgress, ApplyCallback onApply, std::function<void()> onBack)
      : Activity("BaseDescription", renderer, mappedInput),
        background_(background),
        showTitle_(showTitle),
        showAuthor_(showAuthor),
        showProgress_(showProgress),
        onApply_(std::move(onApply)),
        onBack_(std::move(onBack)) {}

  void onEnter() override;
  void loop() override;

 private:
  static constexpr int kRowHeight = 70;

  bool background_ = false;
  bool showTitle_ = true;
  bool showAuthor_ = true;
  bool showProgress_ = true;
  ApplyCallback onApply_;
  std::function<void()> onBack_;

  void render();
  void close();
  void handleTouch(int x, int y);
};

#pragma once

#include <functional>
#include <memory>
#include <utility>

#include "activity/page/Page.h"

/** Main page for the shortcuts previously exposed by Home's hamburger drawer. */
class AppsActivity final : public UiPage {
 public:
  AppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onBack)
      : UiPage("Apps", renderer, mappedInput), onBack_(std::move(onBack)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  const char* name() const override { return "Apps"; }

 protected:
  void content() override;
  bool back() override;

 private:
  static constexpr int kRowHeight = 100;
  static constexpr int kMenuItemCount = 6;

  std::function<void()> onBack_;
  std::unique_ptr<Activity> subActivity_;
  int selectedIndex_ = 0;
  bool subActivityFinished_ = false;

  static int bodyTop();
  int visibleItemCount() const;
  int itemIndex(int visibleIndex) const;
  void enterChild(Activity* activity);
  void openSelected();
};

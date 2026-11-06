#pragma once

#include <functional>

#include "activity/Activity.h"
#include "system/UiLayout.h"

class BaseStatisticsActivity final : public Activity {
 public:
  using ApplyCallback = std::function<void(bool, bool)>;

  BaseStatisticsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool lineChart, bool showSummary,
                         ApplyCallback onApply,
                         std::function<void()> onBack)
      : Activity("BaseStatistics", renderer, mappedInput), lineChart_(lineChart), showSummary_(showSummary),
        onApply_(std::move(onApply)),
        onBack_(std::move(onBack)) {}

  void onEnter() override;
  void loop() override;

 private:
  static constexpr int kRowHeight = UiLayout::LIST_ITEM_HEIGHT;

  bool stylePopup_ = false;
  bool lineChart_ = false;
  bool showSummary_ = true;
  ApplyCallback onApply_;
  std::function<void()> onBack_;

  void render();
  void renderStylePopup();
  void close();
  void handleTouch(int x, int y);
};

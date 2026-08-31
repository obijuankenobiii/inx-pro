#pragma once

#include <functional>

#include "activity/Activity.h"
#include "state/HomeTheme.h"
#include "system/UiLayout.h"

class BaseHeatmapActivity final : public Activity {
 public:
  using ApplyCallback = std::function<void(HomeTheme::HeatmapView)>;

  BaseHeatmapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, HomeTheme::HeatmapView view,
                      ApplyCallback onApply, std::function<void()> onBack)
      : Activity("BaseHeatmap", renderer, mappedInput), view_(view), onApply_(std::move(onApply)),
        onBack_(std::move(onBack)) {}

  void onEnter() override;
  void loop() override;

 private:
  static constexpr int kRowHeight = UiLayout::LIST_ITEM_HEIGHT;
  HomeTheme::HeatmapView view_;
  bool viewPopup_ = false;
  ApplyCallback onApply_;
  std::function<void()> onBack_;

  void render();
  void renderViewPopup();
  void close();
  void handleTouch(int x, int y);
};

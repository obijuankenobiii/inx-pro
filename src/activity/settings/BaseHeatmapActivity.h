#pragma once

#include <functional>

#include "activity/Activity.h"
#include "state/HomeTheme.h"
#include "system/UiLayout.h"

class BaseHeatmapActivity final : public Activity {
 public:
  using ApplyCallback = std::function<void(HomeTheme::HeatmapView, bool, HomeTheme::CarouselLabelColor)>;

  BaseHeatmapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, HomeTheme::HeatmapView view,
                      bool showLabel, HomeTheme::CarouselLabelColor labelColor, ApplyCallback onApply,
                      std::function<void()> onBack)
      : Activity("BaseHeatmap", renderer, mappedInput), view_(view), showLabel_(showLabel), labelColor_(labelColor),
        onApply_(std::move(onApply)), onBack_(std::move(onBack)) {}

  void onEnter() override;
  void loop() override;

 private:
  static constexpr int kRowHeight = UiLayout::LIST_ITEM_HEIGHT;
  HomeTheme::HeatmapView view_;
  bool showLabel_ = true;
  HomeTheme::CarouselLabelColor labelColor_ = HomeTheme::CarouselLabelColor::Black;
  bool viewPopup_ = false;
  bool labelColorPopup_ = false;
  ApplyCallback onApply_;
  std::function<void()> onBack_;

  void render();
  void renderViewPopup();
  void renderLabelColorPopup();
  void close();
  void handleTouch(int x, int y);
};

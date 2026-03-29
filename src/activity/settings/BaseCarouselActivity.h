#pragma once

#include <functional>

#include "activity/Activity.h"
#include "state/HomeTheme.h"
#include "system/UiLayout.h"

class BaseCarouselActivity final : public Activity {
 public:
  using ApplyCallback = std::function<void(HomeTheme::CarouselStyle, bool, bool, HomeTheme::CarouselLabelColor,
                                           HomeTheme::CarouselShadowStyle)>;

  BaseCarouselActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, HomeTheme::CarouselStyle style,
                       bool background, bool showLabel, HomeTheme::CarouselLabelColor labelColor,
                       HomeTheme::CarouselShadowStyle shadowStyle, ApplyCallback onApply, std::function<void()> onBack,
                       bool recentStyle = false)
      : Activity("BaseCarousel", renderer, mappedInput),
        style_(style),
        background_(background),
        showLabel_(showLabel),
        shadowStyle_(shadowStyle),
        labelColor_(labelColor),
        recentStyle_(recentStyle),
        onApply_(std::move(onApply)),
        onBack_(std::move(onBack)) {
    if (recentStyle_ && style_ == HomeTheme::CarouselStyle::Centered) {
      style_ = HomeTheme::CarouselStyle::Left;
    }
  }

  void onEnter() override;
  void loop() override;

 private:
  static constexpr int kRowHeight = UiLayout::LIST_ITEM_HEIGHT;

  HomeTheme::CarouselStyle style_;
  bool background_ = false;
  bool showLabel_ = true;
  bool stylePopup_ = false;
  bool shadowStylePopup_ = false;
  bool labelColorPopup_ = false;
  HomeTheme::CarouselLabelColor labelColor_ = HomeTheme::CarouselLabelColor::Black;
  HomeTheme::CarouselShadowStyle shadowStyle_ = HomeTheme::CarouselShadowStyle::None;
  bool recentStyle_ = false;
  ApplyCallback onApply_;
  std::function<void()> onBack_;

  void render();
  void renderStylePopup();
  void renderShadowStylePopup();
  void renderLabelColorPopup();
  void close();
  void handleTouch(int x, int y);
};

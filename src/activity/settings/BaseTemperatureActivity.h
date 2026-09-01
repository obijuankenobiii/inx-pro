#pragma once

#include <functional>

#include "activity/Activity.h"
#include "system/UiLayout.h"

class BaseTemperatureActivity final : public Activity {
 public:
  using ApplyCallback = std::function<void(bool)>;

  BaseTemperatureActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const bool fahrenheit,
                          ApplyCallback onApply, std::function<void()> onBack)
      : Activity("BaseTemperature", renderer, mappedInput),
        fahrenheit_(fahrenheit),
        onApply_(std::move(onApply)),
        onBack_(std::move(onBack)) {}

  void onEnter() override;
  void loop() override;

 private:
  static constexpr int kRowHeight = UiLayout::LIST_ITEM_HEIGHT;

  bool fahrenheit_ = false;
  bool unitPopup_ = false;
  ApplyCallback onApply_;
  std::function<void()> onBack_;

  void render();
  void renderUnitPopup();
  void close();
  void handleTouch(int x, int y);
};

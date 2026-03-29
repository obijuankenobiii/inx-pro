#pragma once

/**
 * @file ButtonMappingActivity.h
 * @brief Reader button-to-action mapping subpage.
 */

#include <cstdint>
#include <functional>
#include <vector>

#include "activity/ActivityWithSubactivity.h"

class ButtonMappingActivity final : public ActivityWithSubactivity {
 public:
  ButtonMappingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onDone)
      : ActivityWithSubactivity("ButtonMapping", renderer, mappedInput), onDone_(std::move(onDone)) {}

  void onEnter() override;
  void loop() override;

 private:
  static constexpr int kButtonCount = 5;
  static constexpr int kRowHeight = 60;

  void render();
  void openSelector(int row);
  void closeSelector();
  void commitSelector();
  void handleSelectorInput();
  void handleListInput();
  void renderSelector();
  uint8_t* actionSlot(int row);
  const uint8_t* actionSlot(int row) const;

  std::function<void()> onDone_;
  int selectedRow_ = 0;
  bool selectorOpen_ = false;
  int selectorRow_ = -1;
  int selectorSelected_ = 0;
  int selectorScroll_ = 0;
  std::vector<uint8_t> selectorActions_;
};

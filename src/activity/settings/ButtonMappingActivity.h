#pragma once

/**
 * @file ButtonMappingActivity.h
 * @brief Reader button-to-action mapping subpage.
 */

#include <cstdint>
#include <functional>
#include <vector>

#include "activity/ActivityWithSubactivity.h"
#include "system/UiLayout.h"

class ButtonMappingActivity final : public ActivityWithSubactivity {
 public:
  ButtonMappingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onDone)
      : ActivityWithSubactivity("ButtonMapping", renderer, mappedInput), onDone_(std::move(onDone)) {}

  void onEnter() override;
  void loop() override;

 private:
  static constexpr int kButtonCount = 5;
  static constexpr int kGestureCount = 3;
  static constexpr int kQuickActionRow = 0;
  static constexpr int kButtonStartRow = kQuickActionRow + 1;
  static constexpr int kGestureStartRow = kButtonStartRow + kButtonCount;
  static constexpr int kItemCount = kGestureStartRow + kGestureCount;
  static constexpr int kSectionHeaderHeight = 24;
  static constexpr int kRowHeight = UiLayout::LIST_ITEM_HEIGHT;
  static constexpr int kDisableLightRow = kGestureStartRow;
  static constexpr int kPageTurnRow = kGestureStartRow + 1;
  static constexpr int kDoubleTapRow = kGestureStartRow + 2;
  static constexpr int kQuickActionToButtonGap = 20;
  static constexpr int kButtonToGesturesGap = 20;

  void render();
  void openSelector(int row);
  void closeSelector();
  void commitSelector();
  void handleSelectorInput();
  void handleListInput();
  void handleGestureInput(int row);
  void renderSelector();
  uint8_t* actionSlot(int row);
  const uint8_t* actionSlot(int row) const;
  int itemRowAtY(int tapY) const;
  int itemY(int row) const;

  std::function<void()> onDone_;
  int selectedRow_ = 0;
  bool selectorOpen_ = false;
  bool subFinished_ = false;
  int selectorRow_ = -1;
  int selectorSelected_ = 0;
  int selectorScroll_ = 0;
  std::vector<uint8_t> selectorActions_;
};

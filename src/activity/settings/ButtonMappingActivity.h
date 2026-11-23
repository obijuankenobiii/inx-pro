#pragma once

/**
 * @file ButtonMappingActivity.h
 * @brief Reader button-to-action mapping subpage.
 */

#include <cstdint>
#include <functional>
#include <vector>

#include "activity/ActivityWithSubactivity.h"
#include "SettingsListLayout.h"
#include "state/ReaderSetting.h"

class ButtonMappingActivity final : public ActivityWithSubactivity {
 public:
  enum class Section { Main, Buttons, Gestures };

  ButtonMappingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onDone,
                        Section section = Section::Main)
      : ActivityWithSubactivity("ButtonMapping", renderer, mappedInput), onDone_(std::move(onDone)), section_(section) {}

  void onEnter() override;
  void loop() override;

 private:
  static constexpr int kButtonCount = 5;
  static constexpr int kGestureCount = 3;
  static constexpr int kMainRowCount = 5;
  static constexpr int kQuickActionRow = 0;
  static constexpr int kButtonStartRow = kQuickActionRow + 1;
  static constexpr int kGestureStartRow = kButtonStartRow + kButtonCount;
  static constexpr int kItemCount = kGestureStartRow + kGestureCount;
  static constexpr int kSectionHeaderHeight = 24;
  static constexpr int kRowHeight = SettingsListLayout::kRowHeight;
  static constexpr int kTapZoneRow = kGestureStartRow;
  static constexpr int kDoubleTapZoneRow = kGestureStartRow + 1;
  static constexpr int kSwipeZoneRow = kGestureStartRow + 2;
  static constexpr int kMainDisableLightRow = 3;
  static constexpr int kMainDisableHighlightRow = 4;
  static constexpr int kTapZoneSelectorRow = -2;
  static constexpr int kSwipeZoneSelectorRow = -3;
  enum class SwipeDirection : uint8_t { Up, Down, Left, Right };
  static constexpr int kTapZoneColumns = ReaderSetting::TAP_ZONE_COLUMNS;
  static constexpr int kTapZoneRows = ReaderSetting::TAP_ZONE_ROWS;
  static constexpr int kTapZoneCount = ReaderSetting::TAP_ZONE_COUNT;
  static constexpr int kQuickActionToButtonGap = 20;
  static constexpr int kButtonToGesturesGap = 20;

  void render();
  void renderMain();
  void renderSection();
  void openSelector(int row);
  void closeSelector();
  void commitSelector();
  void handleSelectorInput();
  void handleListInput();
  void handleMainInput();
  void handleGestureInput(int row);
  void handleTapGridInput();
  void handleSwipeGridInput();
  void renderSelector();
  void renderTapGrid();
  void renderSwipeGrid();
  void openTapZoneSelector(int cell);
  void openSwipeZoneSelector(int cell, SwipeDirection direction);
  uint8_t* actionSlot(int row);
  const uint8_t* actionSlot(int row) const;
  int itemRowAtY(int tapY) const;
  int itemY(int row) const;
  int sectionRowCount() const;
  int sectionItemY(int row) const;

  std::function<void()> onDone_;
  Section section_ = Section::Main;
  int selectedRow_ = 0;
  bool selectorOpen_ = false;
  bool tapGridOpen_ = false;
  bool swipeGridOpen_ = false;
  bool subFinished_ = false;
  int selectorRow_ = -1;
  int selectorSelected_ = 0;
  int selectorScroll_ = 0;
  int tapZoneCell_ = -1;
  bool tapGridDoubleTap_ = false;
  int swipeZoneCell_ = -1;
  SwipeDirection swipeGridDirection_ = SwipeDirection::Up;
  std::vector<uint8_t> selectorActions_;
};

#pragma once

/**
 * @file QuickActionsSettingsActivity.h
 * @brief Checklist of remaining READER_BUTTON_ACTION values includable in the in-reader popup.
 *
 * Opened from ButtonMappingActivity's Button & Gestures section. Touch-provided tools are intentionally omitted;
 * toggling a row flips its bit in
 * ReaderSetting::quickActionsMask and saves immediately, same as SettingsDrawer's toggle rows. A
 * button mapped to BTN_ACTION_QUICK_ACTIONS (see ReaderPresetsActivity's button pickers) opens
 * QuickActionsMenuUi while reading, which lists whichever actions are checked here.
 */

#include <functional>

#include "activity/ActivityWithSubactivity.h"

class QuickActionsSettingsActivity final : public ActivityWithSubactivity {
 public:
  QuickActionsSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onDone)
      : ActivityWithSubactivity("QuickActionsSettings", renderer, mappedInput), onDone_(std::move(onDone)) {}

  void onEnter() override;
  void loop() override;

 private:
  void render();
  void toggleSelected();

  std::function<void()> onDone_;
  int selectedIndex_ = 0;
  int scrollOffset_ = 0;
};

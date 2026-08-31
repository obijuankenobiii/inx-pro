#pragma once

/**
 * @file ReaderPresetsActivity.h
 * @brief Reader settings and preset management panel.
 *
 * Selecting "Add new" or a preset opens the ReaderPresetEditorActivity (live preview + categorized
 * settings). Confirm on an existing preset opens a small action overlay (Edit / Rename / Delete;
 * Default can only be edited).
 */

#include <functional>
#include "activity/page/Page.h"
#include <string>
#include <vector>

#include "activity/ActivityWithSubactivity.h"
#include "activity/page/navigation/Menu.h"

class ReaderPresetsActivity final : public ActivityWithSubactivity, public navigation::Menu {
 public:
  ReaderPresetsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onGoBack,
                        std::function<void()> tabNavigateRecent = nullptr,
                        std::function<void()> tabNavigateLibrary = nullptr,
                        std::function<void()> tabNavigateSync = nullptr,
                        std::function<void()> tabNavigateStatistics = nullptr,
                        bool embedded = false,
                        bool presetsOnly = false,
                        std::function<void()> hardwareBackHandler = nullptr);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  /** Renders this panel into the parent Page framebuffer without displaying it. */
  void renderEmbedded();
  /** Returns and clears the panel's pending parent-frame request. */
  bool takeRenderRequest();
  /** True while an embedded popup is open over the panel. */
  bool isPopupOpen() const { return embedded_ && (overlayOpen_ || actionSelectorOpen_); }
  /** True while a selector or child settings page owns the full display. */
  bool isDetailOpen() const {
    return embedded_ && (subActivity != nullptr || isPopupOpen());
  }

 private:
  void navigateToSelectedMenu() override;

  void render();
  void renderOverlay();
  int rowCount() const;  ///< Flattened Reader rows or the Presets list rows
  int systemHeaderRow() const { return 0; }
  bool isSystemSettingRow(int row) const;
  void changeSystemSetting(int row, int delta);
  int addPresetRow() const;  ///< "+ Add new preset" row on the Presets page
  int presetRowsStart() const;
  int presetIndexForRow(int row) const;  ///< store index for a preset row, or -1 for the Add-new row
  bool isButtonMappingRow(int row) const;
  bool isQuickActionsRow(int row) const;
  bool isFontManagerRow(int row) const;
  void activateSelectedRow();
  void openEditor(int presetIndex);
  void openRenameKeyboard(int presetIndex);
  void handleOverlayInput();
  void handleListInput();
  void finishSubActivity();
  void clampSelectionToRowCount();

  // Generic popup selector - every multi-option System/XTC row (everything except the plain
  // Text-Anti-Aliasing toggle) opens this via Confirm instead of cycling with Left/Right, same shape
  // as the preset Edit/Rename/Delete overlay. onCommit is called with the chosen option index.
  void openGenericSelector(std::string title, std::vector<std::string> options, int currentIndex,
                           std::function<void(int)> onCommit);
  void handleActionSelectorInput();
  void renderActionSelectorOverlay();
  void openSelectorForRow(int row);  ///< Builds the right options/onCommit for whichever row this is

  const std::function<void()> onGoBack_;
  const std::function<void()> onHardwareBack_;
  const std::function<void()> onTabRecent_;
  const std::function<void()> onTabLibrary_;
  const std::function<void()> onTabSync_;
  const std::function<void()> onTabStatistics_;
  const bool embedded_;
  const bool presetsOnly_;

  static constexpr int kListItemHeight = Page::LIST_ITEM_HEIGHT;
  int selectedRow_ = -1;
  int scrollOffset_ = 0;
  int itemsPerPage_ = 1;
  int listItemHeight_ = kListItemHeight;

  bool overlayOpen_ = false;
  int overlayPresetIndex_ = -1;
  int overlaySel_ = 0;

  bool actionSelectorOpen_ = false;
  std::string selectorTitle_;
  std::vector<std::string> selectorOptions_;
  std::function<void(int)> selectorOnCommit_;
  int actionSelectorSel_ = 0;
  int actionSelectorScroll_ = 0;

  // Deferred sub-activity teardown (editor / rename keyboard) to avoid reentrant deletion.
  bool subFinished_ = false;
  bool updateRequired_ = false;
  int pendingRenameIndex_ = -1;
  std::string pendingRenameName_;
  bool enteredHalfRefresh_ = false;
};

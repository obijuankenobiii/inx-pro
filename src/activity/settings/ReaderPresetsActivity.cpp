/**
 * @file ReaderPresetsActivity.cpp
 * @brief Definitions for ReaderPresetsActivity.
 */

#include "ReaderPresetsActivity.h"
#include "activity/page/Page.h"

#include <Arduino.h>
#include <EpdFontFamily.h>

#include <algorithm>
#include <cstring>

#include "../util/KeyboardEntryActivity.h"
#include "GfxRenderer.h"
#include "activity/page/components/global/PopUp.h"
#include "activity/page/components/global/Toggle.h"
#include "images/Close.h"
#include "images/Download.h"
#include "ReaderFontSettingsDraw.h"
#include "ReaderPresetEditorActivity.h"
#include "ButtonMappingActivity.h"
#include "FontManagerActivity.h"
#include "state/ReaderPreset.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/ScreenComponents.h"

namespace {
constexpr int kRowValueRightInset = 30;
constexpr int kEmbeddedListTopExtra = 30;

struct SettingsTabLayout {
  int systemX;
  int dividerX;
  int readerX;
  int systemW;
  int readerW;
};

SettingsTabLayout settingsTabLayout(const GfxRenderer& renderer) {
  const int font = systemFontId();
  const int systemW = renderer.text.getWidth(font, "System", EpdFontFamily::BOLD);
  const int readerW = renderer.text.getWidth(font, "Reader", EpdFontFamily::BOLD);
  const int dividerX = renderer.getScreenWidth() / 2;
  return {20, dividerX, dividerX + 20, systemW, readerW};
}

const char* overlayOptionFor(const int presetIndex, const int optionIndex) {
  if (presetIndex == 0) {
    static constexpr const char* kDefaultOptions[] = {"Edit", "Cancel"};
    return (optionIndex >= 0 && optionIndex < 2) ? kDefaultOptions[optionIndex] : "";
  }
  static constexpr const char* kPresetOptions[] = {"Edit", "Rename", "Delete", "Cancel"};
  return (optionIndex >= 0 && optionIndex < 4) ? kPresetOptions[optionIndex] : "";
}

int overlayOptionCountFor(const int presetIndex) { return presetIndex == 0 ? 2 : 4; }

MappedInputManager::Button readerSettingsItemPrevButton() {
  return MappedInputManager::Button::Up;
}

MappedInputManager::Button readerSettingsItemNextButton() {
  return MappedInputManager::Button::Down;
}

const char* readerQualityLabel(const uint8_t quality) {
  switch (quality) {
    case SystemSetting::READER_IMAGE_MEDIUM:
      return "Medium";
    case SystemSetting::READER_IMAGE_HIGH:
      return "High";
    default:
      return "Low";
  }
}

const char* systemRefreshLabel() {
  static char buf[12];
  const int pages = READER_SETTINGS.getRefreshFrequency();
  if (pages == 0) {
    return "Off";
  }
  snprintf(buf, sizeof(buf), "%u page%s", pages, pages == 1 ? "" : "s");
  return buf;
}

const char* systemAutoTurnLabel() {
  static char buf[12];
  if (READER_SETTINGS.pageAutoTurnSeconds == 0) {
    return "Off";
  }
  snprintf(buf, sizeof(buf), "%u sec", READER_SETTINGS.pageAutoTurnSeconds);
  return buf;
}

const char* dailyReadingGoalLabel() {
  static char buf[16];
  if (READER_SETTINGS.dailyReadingGoalMinutes == 0) return "Off";
  snprintf(buf, sizeof(buf), "%u min", static_cast<unsigned>(READER_SETTINGS.dailyReadingGoalMinutes));
  return buf;
}

void renderOpenNavigationIcon(const GfxRenderer& renderer, const int screenW, const int itemY, const int rowHeight,
                              const bool invert) {
  constexpr int iconSize = 40;
  const int iconX = screenW - kRowValueRightInset - iconSize;
  const int iconY = itemY + (rowHeight - iconSize) / 2;
  renderer.bitmap.iconScaled(Download, iconX, iconY, iconSize, iconSize, iconSize, iconSize,
                             BitmapRender::Orientation::Rotate270CW, invert);
}

}

ReaderPresetsActivity::ReaderPresetsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const std::function<void()>& onGoBack,
                                             std::function<void()> tabNavigateRecent,
                                             std::function<void()> tabNavigateLibrary,
                                             std::function<void()> tabNavigateSync,
                                             std::function<void()> tabNavigateStatistics,
                                             const bool embedded,
                                             const bool presetsOnly,
                                             std::function<void()> hardwareBackHandler)
    : ActivityWithSubactivity("ReaderPresets", renderer, mappedInput),
      navigation::Menu(renderer),
      onGoBack_(onGoBack),
      onHardwareBack_(std::move(hardwareBackHandler)),
      onTabRecent_(std::move(tabNavigateRecent)),
      onTabLibrary_(std::move(tabNavigateLibrary)),
      onTabSync_(std::move(tabNavigateSync)),
      onTabStatistics_(std::move(tabNavigateStatistics)),
      embedded_(embedded),
      presetsOnly_(presetsOnly) {
  tabSelectorIndex = 2;
}

void ReaderPresetsActivity::onEnter() {
  READER_PRESETS.load();
  const int screenH = renderer.getScreenHeight();
  const int listTop = navigation::Menu::height + 20 + Page::LIST_ITEM_HEIGHT + 10 + kEmbeddedListTopExtra;
  const int contentBottom = screenH - navigation::Menu::bottomHeight - 10;
  if (!presetsOnly_) {
    const int rows = rowCount();
    const int availableHeight = std::max(0, contentBottom - listTop);
    const int minimumReadableRowHeight = renderer.text.getLineHeight(systemFontId()) + 8;
    if (availableHeight >= rows * minimumReadableRowHeight) {
      listItemHeight_ = std::max(1, availableHeight / std::max(1, rows));
      itemsPerPage_ = rows;
    } else {
      listItemHeight_ = kListItemHeight;
      itemsPerPage_ = std::max(1, availableHeight / listItemHeight_);
    }
  } else {
    listItemHeight_ = kListItemHeight;
    itemsPerPage_ = std::max(1, (contentBottom - listTop) / listItemHeight_);
  }
  selectedRow_ = -1;
  scrollOffset_ = 0;
  enteredHalfRefresh_ = false;
  render();
  updateRequired_ = false;
}

void ReaderPresetsActivity::onExit() { exitActivity(); }

constexpr int kSystemFixedRowCount = 5;

bool ReaderPresetsActivity::isSystemSettingRow(const int row) const {
  if (presetsOnly_) return false;
  return row == 0 || (row >= 2 && row < kSystemFixedRowCount + 1);
}

int systemSettingLocalRow(const int row) {
  if (row == 0) return 1;
  if (row >= 2 && row <= kSystemFixedRowCount) return row;
  return -1;
}

void ReaderPresetsActivity::changeSystemSetting(const int row, const int delta) {
  (void)delta;
  const int systemLocalRow = systemSettingLocalRow(row - systemHeaderRow());
  if (systemLocalRow == 1) {
    READER_SETTINGS.textAntiAliasing = !READER_SETTINGS.textAntiAliasing;
  }
  READER_SETTINGS.saveToFile();
}

int ReaderPresetsActivity::addPresetRow() const { return presetsOnly_ ? 0 : -1; }

int ReaderPresetsActivity::presetRowsStart() const { return presetsOnly_ ? 1 : 0; }

int ReaderPresetsActivity::rowCount() const {
  return presetsOnly_ ? presetRowsStart() + READER_PRESETS.count() : kSystemFixedRowCount + 2;
}

int ReaderPresetsActivity::presetIndexForRow(int row) const {
  const int start = presetRowsStart();
  return row < start ? -1 : row - start;
}

bool ReaderPresetsActivity::isButtonMappingRow(const int row) const {
  return !presetsOnly_ && row == kSystemFixedRowCount + 1;
}

bool ReaderPresetsActivity::isFontManagerRow(const int row) const {
  return !presetsOnly_ && row == 1;
}

void ReaderPresetsActivity::navigateToSelectedMenu() {
  if (tabSelectorIndex == 0 && onTabRecent_) {
    onTabRecent_();
  } else if (tabSelectorIndex == 1 && onTabLibrary_) {
    onTabLibrary_();
  } else if (tabSelectorIndex == 3 && onTabSync_) {
    onTabSync_();
  } else if (tabSelectorIndex == 4 && onTabStatistics_) {
    onTabStatistics_();
  }
}

void ReaderPresetsActivity::render() {
  if (embedded_) updateRequired_ = true;
  const int screenW = renderer.getScreenWidth();
  renderer.clearScreen(0xFF);

  const int listTop = navigation::Menu::height + 20 + Page::LIST_ITEM_HEIGHT + 10 + kEmbeddedListTopExtra;

  const int rows = rowCount();
  for (int i = 0; i < itemsPerPage_ && (i + scrollOffset_) < rows; i++) {
    const int rowIndex = i + scrollOffset_;
    const int itemY = listTop + i * listItemHeight_;
    const bool isSelected = (rowIndex == selectedRow_);
    const bool hasNextItem = i + 1 < itemsPerPage_ && rowIndex + 1 < rows;
    const int textY = itemY + (listItemHeight_ - renderer.text.getLineHeight(systemFontId())) / 2;

    if (isSystemSettingRow(rowIndex)) {
      renderer.rectangle.fill(
          0, itemY, screenW, listItemHeight_,
          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
      const char* label = "Text Anti-Aliasing";
      const char* value = nullptr;
      bool isToggle = true;
      bool toggleChecked = READER_SETTINGS.textAntiAliasing != 0;
      const int systemLocalRow = systemSettingLocalRow(rowIndex);
      if (systemLocalRow == 2) {
        label = "Refresh Frequency";
        value = systemRefreshLabel();
        isToggle = false;
      } else if (systemLocalRow == 3) {
        label = "Page Auto Turn";
        value = systemAutoTurnLabel();
        isToggle = false;
      } else if (systemLocalRow == 4) {
        label = "Image Quality";
        value = readerQualityLabel(READER_SETTINGS.readerImageGrayscale);
        isToggle = false;
      } else if (systemLocalRow == 5) {
        label = "Daily Reading Goal";
        value = dailyReadingGoalLabel();
        isToggle = false;
      }
      renderer.text.render(systemFontId(), 20, textY, label, isSelected ? 0 : 1);
      if (isToggle) {
        ReaderFontSettingsDraw::drawToggleCheckbox(renderer, screenW - kRowValueRightInset, itemY, listItemHeight_,
                                                   isSelected, toggleChecked);
      } else {
        const int valueW = renderer.text.getWidth(systemFontId(), value);
        renderer.text.render(systemFontId(), screenW - kRowValueRightInset - valueW, textY, value,
                             isSelected ? 0 : 1);
      }
      if (hasNextItem) {
        renderer.line.render(0, itemY + listItemHeight_ - 1, screenW, itemY + listItemHeight_ - 1, true,
                             LineRender::Style::Dotted);
      }
      continue;
    }

    if (isButtonMappingRow(rowIndex)) {
      renderer.rectangle.fill(
          0, itemY, screenW, listItemHeight_,
          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
      const char* label = "Button & Gestures";
      renderer.text.render(systemFontId(), 20, textY, label, isSelected ? 0 : 1);
      renderOpenNavigationIcon(renderer, screenW, itemY, listItemHeight_, isSelected);
      if (hasNextItem) {
        renderer.line.render(0, itemY + listItemHeight_ - 1, screenW, itemY + listItemHeight_ - 1, true,
                             LineRender::Style::Dotted);
      }
      continue;
    }

    if (isFontManagerRow(rowIndex)) {
      renderer.rectangle.fill(
          0, itemY, screenW, listItemHeight_,
          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
      const char* label = "Font Manager";
      renderer.text.render(systemFontId(), 20, textY, label, isSelected ? 0 : 1);
      renderOpenNavigationIcon(renderer, screenW, itemY, listItemHeight_, isSelected);
      if (hasNextItem) {
        renderer.line.render(0, itemY + listItemHeight_ - 1, screenW, itemY + listItemHeight_ - 1, true,
                             LineRender::Style::Dotted);
      }
      continue;
    }

    if (rowIndex == addPresetRow()) {
      if (isSelected) {
        renderer.rectangle.fill(0, itemY, screenW, listItemHeight_, static_cast<int>(GfxRenderer::FillTone::Ink));
      } else {
        renderer.rectangle.fill(0, itemY, screenW, listItemHeight_, static_cast<int>(GfxRenderer::FillTone::Paper));
      }
      renderer.text.render(systemFontId(), 20, textY, "+ Add new preset", !isSelected,
                           EpdFontFamily::REGULAR);

      if (hasNextItem) {
        renderer.line.render(0, itemY + listItemHeight_ - 1, screenW, itemY + listItemHeight_ - 1, true,
                             LineRender::Style::Dotted);
      }
      continue;
    }

    renderer.rectangle.fill(
        0, itemY, screenW, listItemHeight_,
        isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
    const int presetIndex = presetIndexForRow(rowIndex);
    const std::string name = READER_PRESETS.nameOf(presetIndex);
    renderer.text.render(systemFontId(), 20, textY, name.c_str(), isSelected ? 0 : 1);
    if (presetIndex == 0) {
      const char* tag = "Default";
      const int tagW = renderer.text.getWidth(systemFontId(), tag);
      renderer.text.render(systemFontId(), screenW - kRowValueRightInset - tagW, textY, tag,
                           isSelected ? 0 : 1);
    } else {
      Toggle::render(renderer, screenW - kRowValueRightInset, itemY, listItemHeight_,
                     READER_PRESETS.isPresetDefault(presetIndex), isSelected);
    }
    if (hasNextItem) {
      renderer.line.render(0, itemY + listItemHeight_ - 1, screenW, itemY + listItemHeight_ - 1, true,
                           LineRender::Style::Dotted);
    }
  }
  if (overlayOpen_) {
    renderOverlay();
  }
  if (actionSelectorOpen_) {
    renderActionSelectorOverlay();
  }
}

void ReaderPresetsActivity::renderEmbedded() {
  if (!embedded_ || subActivity) return;
  render();
  updateRequired_ = false;
}

bool ReaderPresetsActivity::takeRenderRequest() {
  if (!embedded_ || subActivity || !updateRequired_) return false;
  updateRequired_ = false;
  return true;
}

void ReaderPresetsActivity::renderOverlay() {
  if (embedded_) updateRequired_ = true;
  const int optionCount = overlayOptionCountFor(overlayPresetIndex_);
  std::vector<std::string> options;
  options.reserve(static_cast<size_t>(optionCount));
  for (int i = 0; i < optionCount; ++i) {
    options.emplace_back(overlayOptionFor(overlayPresetIndex_, i));
  }
  const PopUpBounds box = PopUp::bounds(renderer, optionCount);
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, READER_PRESETS.nameOf(overlayPresetIndex_));
  PopUp::list(renderer, box, options, overlaySel_, 0);
  PopUp::border(renderer, box);

}

void ReaderPresetsActivity::openGenericSelector(std::string title, std::vector<std::string> options,
                                                const int currentIndex, std::function<void(int)> onCommit) {
  selectorTitle_ = std::move(title);
  selectorOptions_ = std::move(options);
  selectorOnCommit_ = std::move(onCommit);
  actionSelectorOpen_ = true;
  actionSelectorSel_ =
      selectorOptions_.empty() ? 0 : std::max(0, std::min(currentIndex, static_cast<int>(selectorOptions_.size()) - 1));
  constexpr int visibleRows = 6;
  actionSelectorScroll_ = std::max(0, actionSelectorSel_ - visibleRows / 2);
  updateRequired_ = true;
}

void ReaderPresetsActivity::openSelectorForRow(const int row) {
  const int systemLocalRow = isSystemSettingRow(row) ? systemSettingLocalRow(row) : -1;
  if (systemLocalRow == 1) return;
  if (systemLocalRow == 2) {
    std::vector<std::string> options = {"1 page", "5 pages", "10 pages", "15 pages", "30 pages", "Off"};
    const int idx = READER_SETTINGS.refreshFrequency < options.size() ? READER_SETTINGS.refreshFrequency : 5;
    openGenericSelector("Refresh Frequency", std::move(options), idx, [](const int chosen) {
      READER_SETTINGS.refreshFrequency = static_cast<uint8_t>(chosen);
      READER_SETTINGS.saveToFile();
    });
    return;
  }
  if (systemLocalRow == 3) {
    std::vector<std::string> options;
    for (int sec = 0; sec <= 180; sec += 10) {
      options.push_back(sec == 0 ? "Off" : (std::to_string(sec) + " sec"));
    }
    const int idx = READER_SETTINGS.pageAutoTurnSeconds / 10;
    openGenericSelector("Page Auto Turn", std::move(options), idx, [](const int chosen) {
      READER_SETTINGS.pageAutoTurnSeconds = static_cast<uint8_t>(chosen * 10);
      READER_SETTINGS.saveToFile();
    });
    return;
  }
  if (systemLocalRow == 4) {
    openGenericSelector("Image Quality", {"Low", "Medium", "High"}, READER_SETTINGS.readerImageGrayscale,
                        [](const int chosen) {
                          READER_SETTINGS.readerImageGrayscale = static_cast<uint8_t>(chosen);
                          READER_SETTINGS.saveToFile();
                        });
    return;
  }
  if (systemLocalRow == 5) {
    std::vector<std::string> options = {"Off"};
    for (int minutes = 5; minutes <= 120; minutes += 5) {
      options.push_back(std::to_string(minutes) + " min");
    }
    const int current = READER_SETTINGS.dailyReadingGoalMinutes == 0
                            ? 0
                            : std::min<int>(24, READER_SETTINGS.dailyReadingGoalMinutes / 5);
    openGenericSelector("Daily Reading Goal", std::move(options), current, [](const int chosen) {
      READER_SETTINGS.dailyReadingGoalMinutes = static_cast<uint8_t>(chosen == 0 ? 0 : chosen * 5);
      READER_SETTINGS.saveToFile();
    });
    return;
  }
}

void ReaderPresetsActivity::handleActionSelectorInput() {
  const int optionCount = static_cast<int>(selectorOptions_.size());
  if (optionCount == 0) {
    actionSelectorOpen_ = false;
    render();
    return;
  }

  if (mappedInput.hasTouch() && (mappedInput.wasTouchSwipeUp() || mappedInput.wasTouchSwipeDown())) {
    const int contentTop = navigation::Menu::height + 20 + Page::LIST_ITEM_HEIGHT + 10 + kEmbeddedListTopExtra;
    const PopUpBounds box = PopUp::bounds(renderer, optionCount, contentTop);
    const int maxScroll = std::max(0, optionCount - box.rows);
    if (mappedInput.wasTouchSwipeUp()) {
      actionSelectorScroll_ = std::min(actionSelectorScroll_ + box.rows, maxScroll);
    } else {
      actionSelectorScroll_ = std::max(0, actionSelectorScroll_ - box.rows);
    }
    actionSelectorSel_ = std::min(actionSelectorScroll_, optionCount - 1);
    renderActionSelectorOverlay();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    actionSelectorOpen_ = false;
    updateRequired_ = true;
    return;
  }

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f, tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int screenW = renderer.getScreenWidth();
      const int screenH = renderer.getScreenHeight();
      const int contentTop = navigation::Menu::height + 20 + Page::LIST_ITEM_HEIGHT + 10 + kEmbeddedListTopExtra;
      const PopUpBounds box = PopUp::bounds(renderer, optionCount, contentTop);
      const int tapX = static_cast<int>(tapNx * screenW);
      const int tapY = static_cast<int>(tapNy * screenH);
      const int optionY = tapY - box.y - box.header;
      if (tapX >= box.x && tapX < box.x + box.width && optionY >= 0 && optionY < box.rows * box.row) {
        const int optionIndex = actionSelectorScroll_ + optionY / box.row;
        if (optionIndex >= 0 && optionIndex < optionCount) {
          actionSelectorSel_ = optionIndex;
          if (selectorOnCommit_) {
            selectorOnCommit_(actionSelectorSel_);
          }
          actionSelectorOpen_ = false;
          if (embedded_) {
            updateRequired_ = true;
          } else {
            render();
          }
          return;
        }
      }
      actionSelectorOpen_ = false;
      if (embedded_) {
        updateRequired_ = true;
      } else {
        render();
      }
      return;
    }
  }

  if (mappedInput.wasPressed(readerSettingsItemPrevButton())) {
    const int contentTop = navigation::Menu::height + 20 + Page::LIST_ITEM_HEIGHT + 10 + kEmbeddedListTopExtra;
    const int visibleRows = PopUp::bounds(renderer, optionCount, contentTop).rows;
    actionSelectorSel_ = (actionSelectorSel_ - 1 + optionCount) % optionCount;
    if (actionSelectorSel_ < actionSelectorScroll_) actionSelectorScroll_ = actionSelectorSel_;
    if (actionSelectorSel_ >= actionSelectorScroll_ + visibleRows) {
      actionSelectorScroll_ = actionSelectorSel_ - visibleRows + 1;
    }
    renderActionSelectorOverlay();
    return;
  }
  if (mappedInput.wasPressed(readerSettingsItemNextButton())) {
    const int contentTop = navigation::Menu::height + 20 + Page::LIST_ITEM_HEIGHT + 10 + kEmbeddedListTopExtra;
    const int visibleRows = PopUp::bounds(renderer, optionCount, contentTop).rows;
    actionSelectorSel_ = (actionSelectorSel_ + 1) % optionCount;
    if (actionSelectorSel_ < actionSelectorScroll_) actionSelectorScroll_ = actionSelectorSel_;
    if (actionSelectorSel_ >= actionSelectorScroll_ + visibleRows) {
      actionSelectorScroll_ = actionSelectorSel_ - visibleRows + 1;
    }
    renderActionSelectorOverlay();
    return;
  }
}

void ReaderPresetsActivity::renderActionSelectorOverlay() {
  if (embedded_) updateRequired_ = true;
  const int optionCount = static_cast<int>(selectorOptions_.size());
  const int contentTop = navigation::Menu::height + 20 + Page::LIST_ITEM_HEIGHT + 10 + kEmbeddedListTopExtra;
  if (optionCount <= 0) return;
  const PopUpBounds box = PopUp::bounds(renderer, optionCount, contentTop);
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, selectorTitle_);
  PopUp::list(renderer, box, selectorOptions_, actionSelectorSel_, actionSelectorScroll_);
  PopUp::border(renderer, box);

}

void ReaderPresetsActivity::openEditor(int presetIndex) {
  enterNewActivity(
      new ReaderPresetEditorActivity(renderer, mappedInput, presetIndex, [this]() { subFinished_ = true; }));
}

void ReaderPresetsActivity::openRenameKeyboard(int presetIndex) {
  const std::string current = READER_PRESETS.nameOf(presetIndex);
  enterNewActivity(new KeyboardEntryActivity(
      renderer, mappedInput, "Rename preset", current, 10, 40, false,
      [this, presetIndex](const std::string& entered) {
        pendingRenameIndex_ = presetIndex;
        pendingRenameName_ = entered;
        subFinished_ = true;
      },
      [this]() { subFinished_ = true; }));
}

void ReaderPresetsActivity::activateSelectedRow() {
  if (selectedRow_ < 0) {
    return;
  }
  if (presetsOnly_) {
    if (selectedRow_ == addPresetRow()) {
      openEditor(-1);
      return;
    }
    if (selectedRow_ >= presetRowsStart()) {
      overlayPresetIndex_ = presetIndexForRow(selectedRow_);
      overlaySel_ = -1;
      overlayOpen_ = true;
      renderOverlay();
      return;
    }
  }
  if (isSystemSettingRow(selectedRow_)) {
    const int systemLocalRow = systemSettingLocalRow(selectedRow_);
    if (systemLocalRow == 1) {
      changeSystemSetting(selectedRow_, 0);
      selectedRow_ = -1;
      render();
      return;
    }
    openSelectorForRow(selectedRow_);
    renderActionSelectorOverlay();
    return;
  }
  if (isButtonMappingRow(selectedRow_)) {
    enterNewActivity(new ButtonMappingActivity(renderer, mappedInput, [this]() { subFinished_ = true; }));
    return;
  }
  if (isFontManagerRow(selectedRow_)) {
    enterNewActivity(new FontManagerActivity(renderer, mappedInput, [this]() { subFinished_ = true; }));
    return;
  }
  if (selectedRow_ == addPresetRow()) {
    openEditor(-1);
    return;
  }
  overlayPresetIndex_ = presetIndexForRow(selectedRow_);
  overlaySel_ = -1;
  overlayOpen_ = true;
  renderOverlay();
}

void ReaderPresetsActivity::clampSelectionToRowCount() {
  const int rows = rowCount();
  if (selectedRow_ >= rows) {
    selectedRow_ = std::max(0, rows - 1);
  }
}

void ReaderPresetsActivity::handleOverlayInput() {
  const int n = overlayOptionCountFor(overlayPresetIndex_);

  if (mappedInput.wasPressed(readerSettingsItemPrevButton())) {
    overlaySel_ = overlaySel_ < 0 ? n - 1 : (overlaySel_ - 1 + n) % n;
    renderOverlay();
    return;
  }
  if (mappedInput.wasPressed(readerSettingsItemNextButton())) {
    overlaySel_ = overlaySel_ < 0 ? 0 : (overlaySel_ + 1) % n;
    renderOverlay();
    return;
  }

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int screenW = renderer.getScreenWidth();
      const int screenH = renderer.getScreenHeight();
      const PopUpBounds box = PopUp::bounds(renderer, n);
      const int tapX = static_cast<int>(tapNx * screenW);
      const int tapY = static_cast<int>(tapNy * screenH);
      const int optionY = tapY - box.y - box.header;
      if (tapX >= box.x && tapX < box.x + box.width && optionY >= 0 && optionY < n * box.row) {
        overlaySel_ = optionY / box.row;
        const char* choice = overlayOptionFor(overlayPresetIndex_, overlaySel_);
        const int presetIndex = overlayPresetIndex_;
        overlayOpen_ = false;
        if (strcmp(choice, "Edit") == 0) {
          openEditor(presetIndex);
        } else if (strcmp(choice, "Rename") == 0) {
          openRenameKeyboard(presetIndex);
        } else if (strcmp(choice, "Delete") == 0) {
          READER_PRESETS.remove(presetIndex);
          const int rows = rowCount();
          if (selectedRow_ >= rows) selectedRow_ = std::max(0, rows - 1);
          render();
        } else {
          render();
        }
      } else {
        overlayOpen_ = false;
        render();
      }
      return;
    }
  }

}

void ReaderPresetsActivity::handleListInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onHardwareBack_) {
      onHardwareBack_();
    } else {
      onGoBack_();
    }
    return;
  }

  if (mappedInput.wasPressed(readerSettingsItemPrevButton())) {
    const int rows = rowCount();
    if (rows > 0) {
      selectedRow_ = (selectedRow_ - 1 + rows) % rows;
      if (selectedRow_ < scrollOffset_) scrollOffset_ = selectedRow_;
      if (selectedRow_ >= scrollOffset_ + itemsPerPage_) scrollOffset_ = selectedRow_ - itemsPerPage_ + 1;
      scrollOffset_ = std::max(0, std::min(scrollOffset_, std::max(0, rows - itemsPerPage_)));
      render();
    }
    return;
  }
  if (mappedInput.wasPressed(readerSettingsItemNextButton())) {
    const int rows = rowCount();
    if (rows > 0) {
      selectedRow_ = (selectedRow_ + 1) % rows;
      if (selectedRow_ < scrollOffset_) scrollOffset_ = selectedRow_;
      if (selectedRow_ >= scrollOffset_ + itemsPerPage_) scrollOffset_ = selectedRow_ - itemsPerPage_ + 1;
      scrollOffset_ = std::max(0, std::min(scrollOffset_, std::max(0, rows - itemsPerPage_)));
      render();
    }
    return;
  }

  if (mappedInput.hasTouch()) {
    if (mappedInput.wasTouchSwipeUp() || mappedInput.wasTouchSwipeDown()) {
      const int rows = rowCount();
      const int maxScroll = std::max(0, rows - itemsPerPage_);
      if (mappedInput.wasTouchSwipeUp()) {
        scrollOffset_ = std::min(maxScroll, scrollOffset_ + 1);
      } else {
        scrollOffset_ = std::max(0, scrollOffset_ - 1);
      }
      render();
      return;
    }

    float tapNx = 0.0f, tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int screenW = renderer.getScreenWidth();
      const int screenH = renderer.getScreenHeight();
      const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());

      const int listTop = navigation::Menu::height + 20 + Page::LIST_ITEM_HEIGHT + 10 + kEmbeddedListTopExtra;
      const int tappedRow = (tapY - listTop) / listItemHeight_;
      const int rowCountValue = rowCount();
      if (tapX >= 0 && tapX < screenW && tapY >= listTop && tappedRow >= 0 &&
          tappedRow < itemsPerPage_ && scrollOffset_ + tappedRow < rowCountValue) {
        selectedRow_ = scrollOffset_ + tappedRow;
        if (presetsOnly_) {
          const int presetIndex = presetIndexForRow(selectedRow_);
          const int itemY = listTop + tappedRow * listItemHeight_;
          const ToggleBounds toggle = Toggle::bounds(screenW - kRowValueRightInset, itemY, listItemHeight_);
          if (presetIndex > 0 && tapX >= toggle.x && tapX < toggle.x + toggle.width && tapY >= toggle.y &&
              tapY < toggle.y + toggle.height) {
            READER_PRESETS.setDefaultPreset(READER_PRESETS.isPresetDefault(presetIndex) ? 0 : presetIndex);
            selectedRow_ = -1;
            render();
            return;
          }
        }
        activateSelectedRow();
        selectedRow_ = -1;
        return;
      }
    }
  }

}

void ReaderPresetsActivity::finishSubActivity() {
  exitActivity();
  if (pendingRenameIndex_ >= 0) {
    READER_PRESETS.rename(pendingRenameIndex_, pendingRenameName_);
    pendingRenameIndex_ = -1;
    pendingRenameName_.clear();
  }
  const int rows = rowCount();
  if (selectedRow_ >= rows) selectedRow_ = std::max(0, rows - 1);
  render();
}

void ReaderPresetsActivity::loop() {
  if (subActivity) {
    ActivityWithSubactivity::loop();
    if (subFinished_) {
      subFinished_ = false;
      finishSubActivity();
    }
    return;
  }

  if (mappedInput.hasTouch() && mappedInput.wasTouchSwipeUpForRenderer(renderer)) {
    return;
  }

  if (actionSelectorOpen_) {
    handleActionSelectorInput();
  } else if (overlayOpen_) {
    handleOverlayInput();
  } else {
    handleListInput();
  }
}

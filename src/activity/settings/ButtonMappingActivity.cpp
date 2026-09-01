/**
 * @file ButtonMappingActivity.cpp
 * @brief Definitions for ButtonMappingActivity.
 */

#include "ButtonMappingActivity.h"

#include <GfxRenderer.h>
#include <BoardConfig.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/PopUp.h"
#include "activity/settings/QuickActionsSettingsActivity.h"
#include "activity/settings/ReaderFontSettingsDraw.h"
#include "images/Download.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiLayout.h"

namespace {
const char* buttonLabel(const int row) {
  static const char* const left = FREEINK_DEVICE_STICKY ? "Up" : "Left";
  static const char* const right = FREEINK_DEVICE_STICKY ? "Down" : "Right";
  static const char* const kLabels[] = {left, "", right, "", "Power"};
  if (row < 0 || row >= 5) return "";
  if (row == 1) return FREEINK_DEVICE_STICKY ? "Up (Long)" : "Left (Long)";
  if (row == 3) return FREEINK_DEVICE_STICKY ? "Down (Long)" : "Right (Long)";
  if (row == 0) return FREEINK_DEVICE_STICKY ? "Up (Short)" : "Left (Short)";
  if (row == 2) return FREEINK_DEVICE_STICKY ? "Down (Short)" : "Right (Short)";
  return kLabels[row];
}

const char* pageTurnLabel(const uint8_t mode) { return mode == ReaderSetting::PAGE_TURN_SWIPE ? "Swipe" : "Tap"; }

bool isSupportedReaderAction(const int action) {
#if FREEINK_DEVICE_X4PRO
  return true;
#else
  return action != SystemSetting::BTN_ACTION_TOGGLE_LIGHT;
#endif
}

std::vector<uint8_t> doubleTapActions() {
  std::vector<uint8_t> actions = {SystemSetting::BTN_ACTION_NONE};
  for (int action = 1; action < static_cast<int>(SystemSetting::READER_BUTTON_ACTION_COUNT); ++action) {
    if (!isSupportedReaderAction(action)) continue;
    actions.push_back(static_cast<uint8_t>(action));
  }
  std::sort(actions.begin() + 1, actions.end(), [](const uint8_t a, const uint8_t b) {
    return strcmp(SystemSetting::readerButtonActionLabel(a), SystemSetting::readerButtonActionLabel(b)) < 0;
  });
  return actions;
}

const char* doubleTapActionLabel(const uint8_t action) {
  return action == SystemSetting::BTN_ACTION_TABLE_OF_CONTENTS ? "Table of Content"
                                                                : SystemSetting::readerButtonActionLabel(action);
}

bool isQuickActionListAction(const int action) {
  return action != SystemSetting::BTN_ACTION_QUICK_ACTIONS && action != SystemSetting::BTN_ACTION_BOOKMARK &&
         action != SystemSetting::BTN_ACTION_DICTIONARY && action != SystemSetting::BTN_ACTION_ANNOTATE &&
         action != SystemSetting::BTN_ACTION_OPEN_SETTINGS && action != SystemSetting::BTN_ACTION_GENERATE_FULL_DATA &&
         action != SystemSetting::BTN_ACTION_GENERATE_THUMBNAIL;
}

std::vector<uint8_t> mappingActions() {
  std::vector<uint8_t> actions = {SystemSetting::BTN_ACTION_NONE};
  for (int action = 1; action < static_cast<int>(SystemSetting::READER_BUTTON_ACTION_COUNT); ++action) {
    if (isSupportedReaderAction(action) && isQuickActionListAction(action)) {
      actions.push_back(static_cast<uint8_t>(action));
    }
  }
  std::sort(actions.begin() + 1, actions.end(), [](const uint8_t a, const uint8_t b) {
    return strcmp(SystemSetting::readerButtonActionLabel(a), SystemSetting::readerButtonActionLabel(b)) < 0;
  });
  actions.push_back(SystemSetting::BTN_ACTION_QUICK_ACTIONS);
  return actions;
}

std::vector<std::string> actionLabels(const std::vector<uint8_t>& actions) {
  std::vector<std::string> labels;
  labels.reserve(actions.size());
  for (const uint8_t action : actions) {
    labels.emplace_back(SystemSetting::readerButtonActionLabel(action));
  }
  return labels;
}
}

void ButtonMappingActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  selectedRow_ = 0;
  selectorOpen_ = false;
  subFinished_ = false;
  render();
}

int ButtonMappingActivity::itemY(const int row) const {
  const int bodyTop = UiLayout::PAGE_HEADER_HEIGHT;
  const int quickActionTop = bodyTop;
  const int buttonHeaderTop = quickActionTop + kRowHeight + kQuickActionToButtonGap;
  const int buttonRowsTop = buttonHeaderTop + kSectionHeaderHeight;
  const int gesturesHeaderTop = buttonRowsTop + kButtonCount * kRowHeight + kButtonToGesturesGap;
  const int gesturesTop = gesturesHeaderTop + kSectionHeaderHeight;
  if (row == kQuickActionRow) return quickActionTop;
  if (row >= kButtonStartRow && row < kGestureStartRow) {
    return buttonRowsTop + (row - kButtonStartRow) * kRowHeight;
  }
  return gesturesTop + (row - kGestureStartRow) * kRowHeight;
}

int ButtonMappingActivity::itemRowAtY(const int tapY) const {
  for (int row = 0; row < kItemCount; ++row) {
    const int y = itemY(row);
    if (tapY >= y && tapY < y + kRowHeight) return row;
  }
  return -1;
}

uint8_t* ButtonMappingActivity::actionSlot(const int row) {
  switch (row) {
    case kButtonStartRow:
      return &READER_SETTINGS.btnLeftAction;
    case kButtonStartRow + 1:
      return &READER_SETTINGS.btnLeftLongAction;
    case kButtonStartRow + 2:
      return &READER_SETTINGS.btnRightAction;
    case kButtonStartRow + 3:
      return &READER_SETTINGS.btnRightLongAction;
    case kButtonStartRow + 4:
      return &READER_SETTINGS.btnPowerShortAction;
    default:
      return nullptr;
  }
}

const uint8_t* ButtonMappingActivity::actionSlot(const int row) const {
  return const_cast<ButtonMappingActivity*>(this)->actionSlot(row);
}

void ButtonMappingActivity::openSelector(const int row) {
  const uint8_t* currentAction = actionSlot(row);
  if (row == kPageTurnRow) {
    selectorActions_ = {ReaderSetting::PAGE_TURN_SWIPE, ReaderSetting::PAGE_TURN_TAP};
  } else if (row == kDoubleTapRow) {
    selectorActions_ = doubleTapActions();
  } else {
    if (!currentAction) return;
    selectorActions_ = mappingActions();
  }
  selectorRow_ = row;
  selectorSelected_ = 0;
  for (int i = 0; i < static_cast<int>(selectorActions_.size()); ++i) {
    const uint8_t current = row == kPageTurnRow
                                ? READER_SETTINGS.pageTurnMode
                                : (row == kDoubleTapRow ? READER_SETTINGS.doubleTapAction : *currentAction);
    if (selectorActions_[static_cast<size_t>(i)] == current) {
      selectorSelected_ = i;
      break;
    }
  }
  selectorScroll_ = std::max(0, selectorSelected_ - (PopUp::maxRows - 1));
  selectorOpen_ = true;
  render();
}

void ButtonMappingActivity::closeSelector() {
  selectorOpen_ = false;
  selectorRow_ = -1;
  selectorSelected_ = 0;
  selectorScroll_ = 0;
  selectorActions_.clear();
  render();
}

void ButtonMappingActivity::commitSelector() {
  if ((selectorRow_ < 0 || selectorRow_ >= kItemCount) || selectorSelected_ < 0 ||
      selectorSelected_ >= static_cast<int>(selectorActions_.size())) {
    closeSelector();
    return;
  }
  if (selectorRow_ == kPageTurnRow) {
    READER_SETTINGS.pageTurnMode = selectorActions_[static_cast<size_t>(selectorSelected_)];
    READER_SETTINGS.saveToFile();
  } else if (selectorRow_ == kDoubleTapRow) {
    READER_SETTINGS.doubleTapAction = selectorActions_[static_cast<size_t>(selectorSelected_)];
    READER_SETTINGS.saveToFile();
  } else if (uint8_t* slot = actionSlot(selectorRow_)) {
    *slot = selectorActions_[static_cast<size_t>(selectorSelected_)];
    READER_SETTINGS.saveToFile();
  }
  closeSelector();
}

void ButtonMappingActivity::handleSelectorInput() {
  const int count = static_cast<int>(selectorActions_.size());
  if (count == 0) {
    closeSelector();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    closeSelector();
    return;
  }

  if (mappedInput.hasTouch() && (mappedInput.wasTouchSwipeUp() || mappedInput.wasTouchSwipeDown())) {
    const PopUpBounds box = PopUp::bounds(renderer, count);
    const int maxScroll = std::max(0, count - box.rows);
    const int page = std::max(1, box.rows);
    if (mappedInput.wasTouchSwipeUp()) {
      selectorScroll_ = std::min(selectorScroll_ + page, maxScroll);
    } else {
      selectorScroll_ = std::max(0, selectorScroll_ - page);
    }
    selectorSelected_ = std::min(selectorScroll_, count - 1);
    render();
    return;
  }

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const PopUpBounds box = PopUp::bounds(renderer, count);
      const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
      const int optionY = tapY - box.y - box.header;
      if (tapX >= box.x && tapX < box.x + box.width && optionY >= 0 && optionY < box.rows * box.row) {
        const int option = selectorScroll_ + optionY / box.row;
        if (option >= 0 && option < count) {
          selectorSelected_ = option;
          commitSelector();
          return;
        }
      }
      closeSelector();
      return;
    }
  }

  const int visibleRows = std::min(PopUp::maxRows, count);
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectorSelected_ = (selectorSelected_ + count - 1) % count;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectorSelected_ = (selectorSelected_ + 1) % count;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    commitSelector();
    return;
  } else {
    return;
  }

  if (selectorSelected_ < selectorScroll_) selectorScroll_ = selectorSelected_;
  if (selectorSelected_ >= selectorScroll_ + visibleRows) selectorScroll_ = selectorSelected_ - visibleRows + 1;
  render();
}

void ButtonMappingActivity::handleListInput() {
  if (SubPage::closeInput(renderer, mappedInput, onDone_)) return;

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
      const int row = itemRowAtY(tapY);
      if (row >= 0) {
        selectedRow_ = row;
        handleGestureInput(row);
        return;
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedRow_ = (selectedRow_ + kItemCount - 1) % kItemCount;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedRow_ = (selectedRow_ + 1) % kItemCount;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleGestureInput(selectedRow_);
  }
}

void ButtonMappingActivity::handleGestureInput(const int row) {
  if (row == kQuickActionRow) {
    enterNewActivity(new QuickActionsSettingsActivity(renderer, mappedInput, [this]() { subFinished_ = true; }));
  } else if (row >= kButtonStartRow && row < kGestureStartRow) {
    openSelector(row);
  } else if (row == kDisableLightRow) {
    READER_SETTINGS.disableLightControl = !READER_SETTINGS.disableLightControl;
    READER_SETTINGS.saveToFile();
    render();
  } else if (row == kPageTurnRow || row == kDoubleTapRow) {
    openSelector(row);
  }
}

void ButtonMappingActivity::renderSelector() {
  std::vector<std::string> labels;
  if (selectorRow_ == kPageTurnRow) {
    labels = {"Swipe", "Tap"};
  } else if (selectorRow_ == kDoubleTapRow) {
    labels.reserve(selectorActions_.size());
    for (const uint8_t action : selectorActions_) labels.emplace_back(doubleTapActionLabel(action));
  } else {
    labels = actionLabels(selectorActions_);
  }
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(labels.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, selectorRow_ == kPageTurnRow
                             ? "Page turn"
                             : (selectorRow_ == kDoubleTapRow ? "Double tap" : buttonLabel(selectorRow_ - kButtonStartRow)));
  PopUp::list(renderer, box, labels, -1, selectorScroll_);
  PopUp::border(renderer, box);
}

void ButtonMappingActivity::render() {
  renderer.clearScreen();
  const int bodyTop = SubPage::header(renderer, "Button & Gestures");
  const int font = systemFontId();
  const int screenW = renderer.getScreenWidth();
  const int valueRight = screenW - 30;
  const int valueLeft = screenW / 2;
  const int valueMaxW = valueRight - valueLeft;

  const int quickActionHeaderY = bodyTop;
  const int quickActionTop = quickActionHeaderY;
  const int buttonHeaderY = quickActionTop + kRowHeight + kQuickActionToButtonGap;
  const int buttonRowsTop = buttonHeaderY + kSectionHeaderHeight;
  const int gesturesHeaderY = buttonRowsTop + kButtonCount * kRowHeight + kButtonToGesturesGap;
  const int gesturesTop = gesturesHeaderY + kSectionHeaderHeight;
  renderer.text.render(font, 20, buttonHeaderY + (kSectionHeaderHeight - renderer.text.getLineHeight(font)) / 2 - 10,
                       "Button", true, EpdFontFamily::BOLD);
  renderer.text.render(font, 20, gesturesHeaderY + (kSectionHeaderHeight - renderer.text.getLineHeight(font)) / 2 - 10,
                       "Gestures", true, EpdFontFamily::BOLD);

  for (int row = 0; row < kItemCount; ++row) {
    const int y = itemY(row);
    const int textY = y + (kRowHeight - renderer.text.getLineHeight(font)) / 2;
    const bool isQuickAction = row == kQuickActionRow;
    const bool isDisableLight = row == kDisableLightRow;
    const bool isPageTurn = row == kPageTurnRow;
    const bool isDoubleTap = row == kDoubleTapRow;
    const char* label = isQuickAction
                            ? "Set up quick actions menu"
                            : (row >= kButtonStartRow && row < kGestureStartRow
                                   ? buttonLabel(row - kButtonStartRow)
                                   : (isDisableLight ? "Disable light control"
                                                      : (isPageTurn ? "Page turn" : "Double tap")));
    renderer.text.render(font, 20, textY, label, true, EpdFontFamily::REGULAR);
    const uint8_t* action = actionSlot(row);
    const char* value = nullptr;
    if (isPageTurn) value = pageTurnLabel(READER_SETTINGS.pageTurnMode);
    else if (isDoubleTap) value = doubleTapActionLabel(READER_SETTINGS.doubleTapAction);
    else if (!isDisableLight && !isQuickAction) value = action ? SystemSetting::readerButtonActionLabel(*action) : "None";
    if (isDisableLight) {
      ReaderFontSettingsDraw::drawToggleCheckbox(renderer, valueRight, y, kRowHeight, false,
                                                 READER_SETTINGS.disableLightControl != 0);
    }
    if (value) {
    const std::string clippedValue = renderer.text.truncate(font, value, valueMaxW);
    const int valueW = renderer.text.getWidth(font, clippedValue.c_str());
    renderer.text.render(font, valueRight - valueW, textY, clippedValue.c_str(), true, EpdFontFamily::REGULAR);
    }
    if (isQuickAction) {
      constexpr int kActionIconSize = 40;
      const int iconX = valueRight - kActionIconSize;
      const int iconY = y + (kRowHeight - kActionIconSize) / 2;
      renderer.bitmap.iconScaled(Download, iconX, iconY, kActionIconSize, kActionIconSize, kActionIconSize,
                                kActionIconSize, BitmapRender::Orientation::Rotate270CW);
    }
    const bool isLastInGroup = row == kQuickActionRow || row == kGestureStartRow - 1 || row == kItemCount - 1;
    if (!isLastInGroup) {
      renderer.line.render(0, y + kRowHeight - 1, screenW, y + kRowHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }

  if (selectorOpen_) renderSelector();
  renderer.displayBuffer();
}

void ButtonMappingActivity::loop() {
  if (subActivity) {
    ActivityWithSubactivity::loop();
    if (subFinished_) {
      subFinished_ = false;
      exitActivity();
      render();
    }
    return;
  }
  if (selectorOpen_) {
    handleSelectorInput();
    return;
  }
  handleListInput();
}

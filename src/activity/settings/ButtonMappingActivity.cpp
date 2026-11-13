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
#include "images/LibraryFilterRight.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiLayout.h"

namespace {
constexpr bool usesVerticalSideButtons() {
  return FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO;
}

const char* previousButtonLabel() { return usesVerticalSideButtons() ? "Up" : "Left"; }
const char* nextButtonLabel() { return usesVerticalSideButtons() ? "Down" : "Right"; }

const char* buttonLabel(const int row) {
  static const char* const kLabels[] = {previousButtonLabel(), "", nextButtonLabel(), "", "Power"};
  if (row < 0 || row >= 5) return "";
  if (row == 1) return usesVerticalSideButtons() ? "Up (Long)" : "Left (Long)";
  if (row == 3) return usesVerticalSideButtons() ? "Down (Long)" : "Right (Long)";
  if (row == 0) return usesVerticalSideButtons() ? "Up (Short)" : "Left (Short)";
  if (row == 2) return usesVerticalSideButtons() ? "Down (Short)" : "Right (Short)";
  return kLabels[row];
}

const char* pageTurnLabel(const uint8_t mode) { return mode == ReaderSetting::PAGE_TURN_SWIPE ? "Swipe" : "Tap"; }

bool isSupportedReaderAction(const int action) {
#if INX_HAS_FRONTLIGHT
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

int ButtonMappingActivity::sectionRowCount() const {
  return section_ == Section::Buttons ? kButtonCount : kGestureCount;
}

int ButtonMappingActivity::sectionItemY(const int row) const {
  return UiLayout::PAGE_HEADER_HEIGHT + row * kRowHeight;
}

int ButtonMappingActivity::itemRowAtY(const int tapY) const {
  if (section_ != Section::Main) {
    const int top = sectionItemY(0);
    const int row = (tapY - top) / kRowHeight;
    return tapY >= top && row >= 0 && row < sectionRowCount() ? row : -1;
  }
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
    const int count = section_ == Section::Main ? 3 : sectionRowCount();
    selectedRow_ = (selectedRow_ + count - 1) % count;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    const int count = section_ == Section::Main ? 3 : sectionRowCount();
    selectedRow_ = (selectedRow_ + 1) % count;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleGestureInput(selectedRow_);
  }
}

void ButtonMappingActivity::handleMainInput() {
  if (SubPage::closeInput(renderer, mappedInput, onDone_)) return;

  const int bodyTop = UiLayout::PAGE_HEADER_HEIGHT;
  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
      const int row = (tapY - bodyTop) / kRowHeight;
      if (tapY >= bodyTop && row >= 0 && row < 3) {
        selectedRow_ = row;
        handleGestureInput(row);
        return;
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedRow_ = (selectedRow_ + 2) % 3;
    render();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedRow_ = (selectedRow_ + 1) % 3;
    render();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleGestureInput(selectedRow_);
  }
}

void ButtonMappingActivity::handleGestureInput(const int row) {
  const int mappedRow = section_ == Section::Buttons
                            ? kButtonStartRow + row
                            : (section_ == Section::Gestures ? kGestureStartRow + row : row);
  if (section_ == Section::Main && row == kQuickActionRow) {
    enterNewActivity(new QuickActionsSettingsActivity(renderer, mappedInput, [this]() { subFinished_ = true; }));
  } else if (section_ == Section::Main && row == kButtonStartRow) {
    enterNewActivity(new ButtonMappingActivity(renderer, mappedInput, [this]() { subFinished_ = true; },
                                               Section::Buttons));
  } else if (section_ == Section::Main && row == kButtonStartRow + 1) {
    enterNewActivity(new ButtonMappingActivity(renderer, mappedInput, [this]() { subFinished_ = true; },
                                               Section::Gestures));
  } else if (section_ == Section::Buttons) {
    openSelector(mappedRow);
  } else if (section_ == Section::Gestures && row == 0) {
    READER_SETTINGS.disableLightControl = !READER_SETTINGS.disableLightControl;
    READER_SETTINGS.saveToFile();
    render();
  } else if (section_ == Section::Gestures && (row == 1 || row == 2)) {
    openSelector(mappedRow);
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

void ButtonMappingActivity::renderMain() {
  const int bodyTop = SubPage::header(renderer, "Button & Gestures");
  const int font = systemFontId();
  const int screenW = renderer.getScreenWidth();
  constexpr int rowCount = 3;
  constexpr int caretWidth = 32;
  constexpr int caretHeight = 30;
  constexpr int rightInset = 30;

  for (int row = 0; row < rowCount; ++row) {
    const int y = bodyTop + row * kRowHeight;
    const int textY = y + (kRowHeight - renderer.text.getLineHeight(font)) / 2;
    const char* label = row == 0 ? "Quick Actions" : row == 1 ? "Buttons" : "Gestures";
    renderer.text.render(font, 20, textY, label, true, EpdFontFamily::REGULAR);
    const int iconX = screenW - rightInset - caretWidth;
    const int iconY = y + (kRowHeight - caretHeight) / 2;
    renderer.bitmap.icon(LibraryFilterRight, iconX, iconY, caretWidth, caretHeight);
    if (row + 1 < rowCount) {
      renderer.line.render(SettingsListLayout::kDividerInset, y + kRowHeight - 1,
                           screenW - SettingsListLayout::kDividerInset, y + kRowHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }
}

void ButtonMappingActivity::renderSection() {
  const char* title = section_ == Section::Buttons ? "Buttons" : "Gestures";
  const int bodyTop = SubPage::header(renderer, title);
  const int font = systemFontId();
  const int screenW = renderer.getScreenWidth();
  const int valueRight = screenW - 30;
  const int valueLeft = screenW / 2;
  const int valueMaxW = valueRight - valueLeft;

  for (int row = 0; row < sectionRowCount(); ++row) {
    const int mappedRow = section_ == Section::Buttons ? kButtonStartRow + row : kGestureStartRow + row;
    const int y = bodyTop + row * kRowHeight;
    const int textY = y + (kRowHeight - renderer.text.getLineHeight(font)) / 2;
    const bool isDisableLight = mappedRow == kDisableLightRow;
    const bool isPageTurn = mappedRow == kPageTurnRow;
    const bool isDoubleTap = mappedRow == kDoubleTapRow;
    const char* label = section_ == Section::Buttons
                            ? buttonLabel(row)
                            : (isDisableLight ? "Disable light control"
                                               : (isPageTurn ? "Page turn" : "Double tap"));
    renderer.text.render(font, 20, textY, label, true, EpdFontFamily::REGULAR);

    const uint8_t* action = actionSlot(mappedRow);
    const char* value = nullptr;
    if (isPageTurn) value = pageTurnLabel(READER_SETTINGS.pageTurnMode);
    else if (isDoubleTap) value = doubleTapActionLabel(READER_SETTINGS.doubleTapAction);
    else if (!isDisableLight) value = action ? SystemSetting::readerButtonActionLabel(*action) : "None";
    if (isDisableLight) {
      ReaderFontSettingsDraw::drawToggleCheckbox(renderer, valueRight, y, kRowHeight, false,
                                                 READER_SETTINGS.disableLightControl != 0);
    }
    if (value) {
      const std::string clippedValue = renderer.text.truncate(font, value, valueMaxW);
      const int valueW = renderer.text.getWidth(font, clippedValue.c_str());
      renderer.text.render(font, valueRight - valueW, textY, clippedValue.c_str(), true, EpdFontFamily::REGULAR);
    }
    if (row + 1 < sectionRowCount()) {
      renderer.line.render(SettingsListLayout::kDividerInset, y + kRowHeight - 1,
                           screenW - SettingsListLayout::kDividerInset, y + kRowHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }
}

void ButtonMappingActivity::render() {
  renderer.clearScreen();
  if (section_ == Section::Main) {
    renderMain();
  } else {
    renderSection();
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
  if (section_ == Section::Main) {
    handleMainInput();
  } else {
    handleListInput();
  }
}

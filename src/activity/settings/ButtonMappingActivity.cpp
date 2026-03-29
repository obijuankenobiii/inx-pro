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
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiLayout.h"

namespace {
const char* buttonLabel(const int row) {
  static const char* const left = FREEINK_DEVICE_STICKY ? "Up Button" : "Left Button";
  static const char* const right = FREEINK_DEVICE_STICKY ? "Down Button" : "Right Button";
  static const char* const kLabels[] = {left, "", right, "", "Power Button"};
  if (row < 0 || row >= 5) return "";
  if (row == 1) return FREEINK_DEVICE_STICKY ? "Up Button (Long)" : "Left Button (Long)";
  if (row == 3) return FREEINK_DEVICE_STICKY ? "Down Button (Long)" : "Right Button (Long)";
  if (row == 0) return FREEINK_DEVICE_STICKY ? "Up Button (Short)" : "Left Button (Short)";
  if (row == 2) return FREEINK_DEVICE_STICKY ? "Down Button (Short)" : "Right Button (Short)";
  return kLabels[row];
}

bool isQuickActionListAction(const int action) {
  return action != SystemSetting::BTN_ACTION_QUICK_ACTIONS && action != SystemSetting::BTN_ACTION_BOOKMARK &&
         action != SystemSetting::BTN_ACTION_DICTIONARY && action != SystemSetting::BTN_ACTION_ANNOTATE &&
         action != SystemSetting::BTN_ACTION_TABLE_OF_CONTENTS && action != SystemSetting::BTN_ACTION_OPEN_SETTINGS &&
         action != SystemSetting::BTN_ACTION_GENERATE_FULL_DATA &&
         action != SystemSetting::BTN_ACTION_GENERATE_THUMBNAIL;
}

std::vector<uint8_t> mappingActions() {
  std::vector<uint8_t> actions = {SystemSetting::BTN_ACTION_NONE};
  for (int action = 1; action < static_cast<int>(SystemSetting::READER_BUTTON_ACTION_COUNT); ++action) {
    if (isQuickActionListAction(action)) {
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
}  // namespace

void ButtonMappingActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  selectedRow_ = 0;
  selectorOpen_ = false;
  render();
}

uint8_t* ButtonMappingActivity::actionSlot(const int row) {
  switch (row) {
    case 0:
      return &READER_SETTINGS.btnLeftAction;
    case 1:
      return &READER_SETTINGS.btnLeftLongAction;
    case 2:
      return &READER_SETTINGS.btnRightAction;
    case 3:
      return &READER_SETTINGS.btnRightLongAction;
    case 4:
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
  if (!currentAction) return;

  selectorActions_ = mappingActions();
  selectorRow_ = row;
  selectorSelected_ = 0;
  for (int i = 0; i < static_cast<int>(selectorActions_.size()); ++i) {
    if (selectorActions_[static_cast<size_t>(i)] == *currentAction) {
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
  if (selectorRow_ < 0 || selectorRow_ >= kButtonCount || selectorSelected_ < 0 ||
      selectorSelected_ >= static_cast<int>(selectorActions_.size())) {
    closeSelector();
    return;
  }
  if (uint8_t* slot = actionSlot(selectorRow_)) {
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
      const int bodyTop = UiLayout::PAGE_HEADER_HEIGHT;
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
      if (tapY >= bodyTop && tapY < bodyTop + kButtonCount * kRowHeight) {
        selectedRow_ = (tapY - bodyTop) / kRowHeight;
        openSelector(selectedRow_);
        return;
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedRow_ = (selectedRow_ + kButtonCount - 1) % kButtonCount;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedRow_ = (selectedRow_ + 1) % kButtonCount;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    openSelector(selectedRow_);
  }
}

void ButtonMappingActivity::renderSelector() {
  const std::vector<std::string> labels = actionLabels(selectorActions_);
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(labels.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, buttonLabel(selectorRow_));
  // Touch selects an option directly. Keep the keyboard cursor for hardware
  // navigation, but do not show a selected/highlighted row.
  PopUp::list(renderer, box, labels, -1, selectorScroll_);
  PopUp::border(renderer, box);
}

void ButtonMappingActivity::render() {
  renderer.clearScreen();
  const int bodyTop = SubPage::header(renderer, "Button Mapping");
  const int font = systemFontId();
  const int screenW = renderer.getScreenWidth();
  const int valueRight = screenW - 30;
  const int valueLeft = screenW / 2;
  const int valueMaxW = valueRight - valueLeft;

  for (int row = 0; row < kButtonCount; ++row) {
    const int y = bodyTop + row * kRowHeight;
    const int textY = y + (kRowHeight - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, 20, textY, buttonLabel(row), true, EpdFontFamily::REGULAR);
    const uint8_t* action = actionSlot(row);
    const char* value = action ? SystemSetting::readerButtonActionLabel(*action) : "None";
    const std::string clippedValue = renderer.text.truncate(font, value, valueMaxW);
    const int valueW = renderer.text.getWidth(font, clippedValue.c_str());
    renderer.text.render(font, valueRight - valueW, textY, clippedValue.c_str(), true, EpdFontFamily::REGULAR);
    renderer.line.render(0, y + kRowHeight - 1, screenW, y + kRowHeight - 1, true, LineRender::Style::Dotted);
  }

  if (selectorOpen_) renderSelector();
  renderer.displayBuffer();
}

void ButtonMappingActivity::loop() {
  if (selectorOpen_) {
    handleSelectorInput();
    return;
  }
  handleListInput();
}

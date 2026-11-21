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
#include "activity/page/navigation/Menu.h"
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

constexpr int kTapGridInset = 20;
constexpr int kTapGridTopPadding = 10;
constexpr int kTapGridBottomPadding = 20;
constexpr int kTapGridGap = 0;
constexpr int kTapGridDotSpacing = 4;

int tapGridBodyTop() {
  return navigation::Menu::topPadding + navigation::Menu::iconSize + 20;
}

struct TapGridCellBounds {
  int x;
  int y;
  int width;
  int height;
};

TapGridCellBounds tapGridCellBounds(const GfxRenderer& renderer, const int cell) {
  const int gridX = kTapGridInset;
  const int gridY = tapGridBodyTop() + kTapGridTopPadding;
  const int gridWidth = renderer.getScreenWidth() - kTapGridInset * 2;
  const int gridHeight = renderer.getScreenHeight() - gridY - kTapGridBottomPadding;
  const int cellWidth = (gridWidth - kTapGridGap * (ReaderSetting::TAP_ZONE_COLUMNS - 1)) /
                        ReaderSetting::TAP_ZONE_COLUMNS;
  const int cellHeight = (gridHeight - kTapGridGap * (ReaderSetting::TAP_ZONE_ROWS - 1)) /
                         ReaderSetting::TAP_ZONE_ROWS;
  const int column = cell % ReaderSetting::TAP_ZONE_COLUMNS;
  const int row = cell / ReaderSetting::TAP_ZONE_COLUMNS;
  return {gridX + column * (cellWidth + kTapGridGap), gridY + row * (cellHeight + kTapGridGap), cellWidth,
          cellHeight};
}

TapGridCellBounds swipeGridCellBounds(const GfxRenderer& renderer, const int cell) {
  const int gridX = kTapGridInset;
  const int gridY = tapGridBodyTop() + kTapGridTopPadding;
  const int gridWidth = renderer.getScreenWidth() - kTapGridInset * 2;
  const int gridHeight = renderer.getScreenHeight() - gridY - kTapGridBottomPadding;
  const int cellWidth = (gridWidth - kTapGridGap * (ReaderSetting::SWIPE_ZONE_COLUMNS - 1)) /
                        ReaderSetting::SWIPE_ZONE_COLUMNS;
  const int cellHeight = (gridHeight - kTapGridGap * (ReaderSetting::SWIPE_ZONE_ROWS - 1)) /
                         ReaderSetting::SWIPE_ZONE_ROWS;
  const int column = cell % ReaderSetting::SWIPE_ZONE_COLUMNS;
  const int row = 0;
  return {gridX + column * (cellWidth + kTapGridGap), gridY + row * (cellHeight + kTapGridGap), cellWidth,
          cellHeight};
}

std::vector<std::string> wrapTapZoneLabel(const GfxRenderer& renderer, const int font, const std::string& label,
                                          const int maxWidth) {
  std::vector<std::string> lines;
  std::string current;
  size_t start = 0;
  while (start < label.size()) {
    const size_t end = label.find(' ', start);
    const std::string word = label.substr(start, end == std::string::npos ? std::string::npos : end - start);
    const std::string candidate = current.empty() ? word : current + " " + word;
    if (!current.empty() && renderer.text.getWidth(font, candidate.c_str()) > maxWidth) {
      lines.push_back(current);
      current = word;
    } else {
      current = candidate;
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  if (!current.empty()) lines.push_back(current);
  return lines;
}

void drawTapZoneDither(const GfxRenderer& renderer, const TapGridCellBounds& bounds) {
  constexpr int inset = 1;
  int row = 0;
  for (int y = bounds.y + inset; y < bounds.y + bounds.height - inset; y += kTapGridDotSpacing, ++row) {
    const int stagger = (row & 1) == 0 ? 0 : kTapGridDotSpacing / 2;
    for (int x = bounds.x + inset + stagger; x < bounds.x + bounds.width - inset; x += kTapGridDotSpacing) {
      renderer.drawPixel(x, y, true);
    }
  }
}

void renderCenteredRotatedLabel(const GfxRenderer& renderer, const TapGridCellBounds& bounds, const int font,
                                const int centerX, const int centerY, const char* label) {
  if (!label || *label == '\0') return;
  const int sourceWidth = renderer.text.getWidth(font, label);
  const int textHeight = renderer.text.getLineHeight(font);
  const int topInset = renderer.text.getFontTopInset(font);
  renderer.text.rotated90CW(font, centerX - textHeight / 2 - topInset, centerY + sourceWidth / 2,
                            label, true, EpdFontFamily::REGULAR);
}

void drawVerticalSwipeAction(const GfxRenderer& renderer, const TapGridCellBounds& bounds, const int font,
                             const int labelCenterX, const uint8_t action, const bool up,
                             const char* fallbackLabel) {
  if (action == SystemSetting::BTN_ACTION_NONE && (!fallbackLabel || *fallbackLabel == '\0')) return;
  constexpr int arrowSize = 13;
  const int labelHeight = renderer.text.getLineHeight(font);

  const int arrowX = labelCenterX + labelHeight / 2 + 20 + arrowSize;


  const int arrowTipY = up ? bounds.y + 14 : bounds.y + bounds.height - 14;
  const int baseY = up ? arrowTipY + arrowSize : arrowTipY - arrowSize;
  const int pointsX[] = {arrowX, arrowX - arrowSize, arrowX + arrowSize};
  const int pointsY[] = {arrowTipY, baseY, baseY};
  renderer.polygon.render(pointsX, pointsY, 3, true, true);
  if (up) {
    renderer.line.render(arrowX, baseY, arrowX, bounds.y + bounds.height - 8, true);
  } else {
    renderer.line.render(arrowX, bounds.y + 8, arrowX, baseY, true);
  }
  const char* label = action == SystemSetting::BTN_ACTION_NONE
                          ? fallbackLabel
                          : SystemSetting::readerButtonActionLabel(action);
  const int labelWidth = renderer.text.getWidth(font, label);

  const int labelCenterY = up ? bounds.y + bounds.height - 20 - labelWidth / 2
                              : bounds.y + 20 + labelWidth / 2;
  renderCenteredRotatedLabel(renderer, bounds, font, labelCenterX, labelCenterY, label);
}

void drawHorizontalSwipeAction(const GfxRenderer& renderer, const TapGridCellBounds& bounds, const int font,
                               const int rowCenterY, const uint8_t action, const bool left,
                               const char* fallbackLabel) {
  if (action == SystemSetting::BTN_ACTION_NONE && (!fallbackLabel || *fallbackLabel == '\0')) return;
  constexpr int arrowSize = 13;
  constexpr int arrowGap = 20;
  constexpr int labelInset = 20;
  const char* label = action == SystemSetting::BTN_ACTION_NONE
                          ? fallbackLabel
                          : SystemSetting::readerButtonActionLabel(action);
  const int labelLineHeight = renderer.text.getLineHeight(font);

  const int arrowY = rowCenterY + labelLineHeight / 2 + 20 + arrowSize;





  const int arrowTipX = left ? bounds.x - bounds.width / 2 + arrowGap / 2
                             : bounds.x + bounds.width + bounds.width / 2 - arrowGap / 2;
  const int baseX = left ? arrowTipX + arrowSize : arrowTipX - arrowSize;
  const int pointsX[] = {arrowTipX, baseX, baseX};
  const int pointsY[] = {arrowY, arrowY - arrowSize, arrowY + arrowSize};
  renderer.polygon.render(pointsX, pointsY, 3, true, true);
  if (left) {
    renderer.line.render(baseX, arrowY, bounds.x + bounds.width - 12, arrowY, true);
  } else {
    renderer.line.render(bounds.x + 12, arrowY, baseX, arrowY, true);
  }

  const int labelWidth = renderer.text.getWidth(font, label);



  const int labelX = left ? bounds.x + bounds.width - labelInset - labelWidth : bounds.x + labelInset;
  renderer.text.render(font, labelX, rowCenterY - labelLineHeight / 2, label, true,
                       EpdFontFamily::REGULAR);
}

void renderSwipeGestureIndicator(const GfxRenderer& renderer, const TapGridCellBounds& bounds, const int font,
                                 const uint8_t upAction, const uint8_t downAction, const uint8_t leftAction,
                                 const uint8_t rightAction, const char* upFallback, const char* downFallback,
                                 const char* leftFallback, const char* rightFallback) {
  const bool hasUp = upAction != SystemSetting::BTN_ACTION_NONE || (upFallback && *upFallback != '\0');
  const bool hasDown = downAction != SystemSetting::BTN_ACTION_NONE || (downFallback && *downFallback != '\0');
  const bool hasLeft = leftAction != SystemSetting::BTN_ACTION_NONE || (leftFallback && *leftFallback != '\0');
  const bool hasRight = rightAction != SystemSetting::BTN_ACTION_NONE || (rightFallback && *rightFallback != '\0');
  if (hasUp || hasDown) {
    const int center = bounds.x + bounds.width / 2;
    if (hasUp && hasDown) {
      drawVerticalSwipeAction(renderer, bounds, font, center - 22, upAction, true, upFallback);
      drawVerticalSwipeAction(renderer, bounds, font, center + 22, downAction, false, downFallback);
    } else if (hasUp) {
      drawVerticalSwipeAction(renderer, bounds, font, center, upAction, true, upFallback);
    } else {
      drawVerticalSwipeAction(renderer, bounds, font, center, downAction, false, downFallback);
    }
  }
  if (hasLeft || hasRight) {
    const int centerY = bounds.y + bounds.height / 2;
    if (hasLeft && hasRight) {
      drawHorizontalSwipeAction(renderer, bounds, font, centerY - 42, leftAction, true, leftFallback);
      drawHorizontalSwipeAction(renderer, bounds, font, centerY + 42, rightAction, false, rightFallback);
    } else if (hasLeft) {
      drawHorizontalSwipeAction(renderer, bounds, font, centerY - 20, leftAction, true, leftFallback);
    } else {
      drawHorizontalSwipeAction(renderer, bounds, font, centerY - 20, rightAction, false, rightFallback);
    }
  }
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
  tapGridOpen_ = false;
  swipeGridOpen_ = false;
  tapZoneCell_ = -1;
  swipeZoneCell_ = -1;
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
  if (selectorRow_ == kTapZoneSelectorRow) {
    if (tapZoneCell_ >= 0 && tapZoneCell_ < kTapZoneCount && selectorSelected_ >= 0 &&
        selectorSelected_ < static_cast<int>(selectorActions_.size())) {
      const uint8_t action = selectorActions_[static_cast<size_t>(selectorSelected_)];
      if (tapGridDoubleTap_) {
        READER_SETTINGS.doubleTapZoneActions[tapZoneCell_] = action;
      } else {
        READER_SETTINGS.tapZoneActions[tapZoneCell_] = action;
      }
      READER_SETTINGS.saveToFile();
    }
    closeSelector();
    return;
  }

  if (selectorRow_ == kSwipeZoneSelectorRow) {
    if (swipeZoneCell_ >= 0 && swipeZoneCell_ < ReaderSetting::SWIPE_ZONE_COUNT && selectorSelected_ >= 0 &&
        selectorSelected_ < static_cast<int>(selectorActions_.size())) {
      const uint8_t action = selectorActions_[static_cast<size_t>(selectorSelected_)];
      switch (swipeGridDirection_) {
        case SwipeDirection::Up:
          READER_SETTINGS.swipeUpZoneActions[swipeZoneCell_] = action;
          break;
        case SwipeDirection::Down:
          READER_SETTINGS.swipeDownZoneActions[swipeZoneCell_] = action;
          break;
        case SwipeDirection::Left:
          READER_SETTINGS.swipeLeftZoneActions[swipeZoneCell_] = action;
          break;
        case SwipeDirection::Right:
          READER_SETTINGS.swipeRightZoneActions[swipeZoneCell_] = action;
          break;
      }
      READER_SETTINGS.saveToFile();
    }
    closeSelector();
    return;
  }

  if ((selectorRow_ < 0 || selectorRow_ >= kItemCount) || selectorSelected_ < 0 ||
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

void ButtonMappingActivity::openTapZoneSelector(const int cell) {
  if (cell < 0 || cell >= kTapZoneCount) return;
  selectorActions_ = doubleTapActions();
  selectorRow_ = kTapZoneSelectorRow;
  tapZoneCell_ = cell;
  selectorSelected_ = 0;
  for (int i = 0; i < static_cast<int>(selectorActions_.size()); ++i) {
    const uint8_t currentAction = tapGridDoubleTap_ ? READER_SETTINGS.doubleTapZoneActions[cell]
                                                    : READER_SETTINGS.tapZoneActions[cell];
    if (selectorActions_[static_cast<size_t>(i)] == currentAction) {
      selectorSelected_ = i;
      break;
    }
  }
  selectorScroll_ = std::max(0, selectorSelected_ - (PopUp::maxRows - 1));
  selectorOpen_ = true;
  render();
}

void ButtonMappingActivity::openSwipeZoneSelector(const int cell, const SwipeDirection direction) {
  if (cell < 0 || cell >= ReaderSetting::SWIPE_ZONE_COUNT) return;
  selectorActions_ = doubleTapActions();
  selectorRow_ = kSwipeZoneSelectorRow;
  swipeZoneCell_ = cell;
  swipeGridDirection_ = direction;
  selectorSelected_ = 0;
  uint8_t currentAction = SystemSetting::BTN_ACTION_NONE;
  switch (direction) {
    case SwipeDirection::Up:
      currentAction = READER_SETTINGS.swipeUpZoneActions[cell];
      break;
    case SwipeDirection::Down:
      currentAction = READER_SETTINGS.swipeDownZoneActions[cell];
      break;
    case SwipeDirection::Left:
      currentAction = READER_SETTINGS.swipeLeftZoneActions[cell];
      break;
    case SwipeDirection::Right:
      currentAction = READER_SETTINGS.swipeRightZoneActions[cell];
      break;
  }
  for (int i = 0; i < static_cast<int>(selectorActions_.size()); ++i) {
    if (selectorActions_[static_cast<size_t>(i)] == currentAction) {
      selectorSelected_ = i;
      break;
    }
  }
  selectorScroll_ = std::max(0, selectorSelected_ - (PopUp::maxRows - 1));
  selectorOpen_ = true;
  render();
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

void ButtonMappingActivity::handleTapGridInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      (mappedInput.hasTouch() && mappedInput.wasTouchSwipeUpForRenderer(renderer))) {
    tapGridOpen_ = false;
    selectorOpen_ = false;
    render();
    return;
  }

  if (SubPage::closeInput(renderer, mappedInput, onDone_)) {
    tapGridOpen_ = false;
    return;
  }

  if (!mappedInput.hasTouch()) return;

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) return;

  const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
  for (int cell = 0; cell < kTapZoneCount; ++cell) {
    const TapGridCellBounds bounds = tapGridCellBounds(renderer, cell);
    if (tapX >= bounds.x && tapX < bounds.x + bounds.width && tapY >= bounds.y &&
        tapY < bounds.y + bounds.height) {


      openTapZoneSelector(cell);
      return;
    }
  }
}

void ButtonMappingActivity::handleSwipeGridInput() {
  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
      for (int cell = 0; cell < ReaderSetting::SWIPE_ZONE_COLUMNS; ++cell) {
        const TapGridCellBounds bounds = swipeGridCellBounds(renderer, cell);
        if (tapX < bounds.x || tapX >= bounds.x + bounds.width || tapY < bounds.y ||
            tapY >= bounds.y + bounds.height)
          continue;

        const int thirdHeight = bounds.height / 3;
        const int middleTop = bounds.y + thirdHeight;
        const int middleBottom = bounds.y + thirdHeight * 2;
        SwipeDirection direction;
        bool selectable = true;
        if (cell == 1) {



          direction = tapX < bounds.x + bounds.width / 2 ? SwipeDirection::Left : SwipeDirection::Right;
        } else if (tapY >= middleTop && tapY < middleBottom && cell == 0) {
          direction = SwipeDirection::Right;
        } else if (tapY >= middleTop && tapY < middleBottom && cell == ReaderSetting::SWIPE_ZONE_COLUMNS - 1) {
          direction = SwipeDirection::Left;
        } else if (tapY < middleTop) {
          direction = SwipeDirection::Down;
          selectable = cell != 1;
        } else if (tapY >= middleBottom) {
          direction = SwipeDirection::Up;
          selectable = cell != 1;
        } else {
          selectable = false;
        }
        if (selectable) openSwipeZoneSelector(cell, direction);
        return;
      }
      return;
    }

    float startNx = 0.0f;
    float startNy = 0.0f;
    SwipeDirection direction;
    bool swipeDetected = false;
    if (mappedInput.wasTouchSwipeUpInScreen(renderer, startNx, startNy)) {
      direction = SwipeDirection::Up;
      swipeDetected = true;
    } else if (mappedInput.wasTouchSwipeDownInScreen(renderer, startNx, startNy)) {
      direction = SwipeDirection::Down;
      swipeDetected = true;
    } else if (mappedInput.wasTouchSwipeLeftInScreen(renderer, startNx, startNy)) {
      direction = SwipeDirection::Left;
      swipeDetected = true;
    } else if (mappedInput.wasTouchSwipeRightInScreen(renderer, startNx, startNy)) {
      direction = SwipeDirection::Right;
      swipeDetected = true;
    }

    if (swipeDetected) {


      if (direction == SwipeDirection::Up && startNy >= 0.95f) {
        swipeGridOpen_ = false;
        selectorOpen_ = false;
        render();
        return;
      }

      const int startX = static_cast<int>(startNx * renderer.getScreenWidth());
      const int startY = static_cast<int>(startNy * renderer.getScreenHeight());
      for (int cell = 0; cell < ReaderSetting::SWIPE_ZONE_COLUMNS; ++cell) {
        const TapGridCellBounds bounds = swipeGridCellBounds(renderer, cell);
        if (startX >= bounds.x && startX < bounds.x + bounds.width && startY >= bounds.y &&
            startY < bounds.y + bounds.height) {
          const bool rightZone = cell == ReaderSetting::SWIPE_ZONE_COLUMNS - 1;
          const bool verticalDirection = direction == SwipeDirection::Up || direction == SwipeDirection::Down;
          const bool horizontalDirection = direction == SwipeDirection::Left || direction == SwipeDirection::Right;
          const bool directionAllowed = cell == 0
                                            ? (verticalDirection || direction == SwipeDirection::Right)
                                            : cell == ReaderSetting::SWIPE_ZONE_COLUMNS - 1
                                                ? (verticalDirection || direction == SwipeDirection::Left)
                                                : horizontalDirection;
          if (directionAllowed) openSwipeZoneSelector(cell, direction);


          return;
        }
      }
      return;
    }
  }

  if (SubPage::closeInput(renderer, mappedInput, [this]() {
        swipeGridOpen_ = false;
        selectorOpen_ = false;
        render();
      })) {
    return;
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
      if (tapY >= bodyTop && row >= 0 && row < kMainRowCount) {
        selectedRow_ = row;
        handleGestureInput(row);
        return;
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedRow_ = (selectedRow_ + kMainRowCount - 1) % kMainRowCount;
    render();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedRow_ = (selectedRow_ + 1) % kMainRowCount;
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
  } else if (section_ == Section::Main && row == kMainDisableLightRow) {
    READER_SETTINGS.disableLightControl = !READER_SETTINGS.disableLightControl;
    READER_SETTINGS.saveToFile();
    render();
  } else if (section_ == Section::Main && row == kMainDisableHighlightRow) {
    READER_SETTINGS.disableHighlight = !READER_SETTINGS.disableHighlight;
    READER_SETTINGS.saveToFile();
    render();
  } else if (section_ == Section::Buttons) {
    openSelector(mappedRow);
  } else if (section_ == Section::Gestures && row == 0) {
    tapGridDoubleTap_ = false;
    tapGridOpen_ = true;
    render();
  } else if (section_ == Section::Gestures && row == 1) {
    tapGridDoubleTap_ = true;
    tapGridOpen_ = true;
    render();
  } else if (section_ == Section::Gestures && row == 2) {
    swipeGridOpen_ = true;
    render();
  }
}

void ButtonMappingActivity::renderSelector() {
  const std::vector<std::string> labels = actionLabels(selectorActions_);
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(labels.size()));
  PopUp::background(renderer, box);
  const char* swipeTitle = "Swipe area";
  if (selectorRow_ == kSwipeZoneSelectorRow) {
    switch (swipeGridDirection_) {
      case SwipeDirection::Up:
        swipeTitle = "Swipe up area";
        break;
      case SwipeDirection::Down:
        swipeTitle = "Swipe down area";
        break;
      case SwipeDirection::Left:
        swipeTitle = "Swipe left area";
        break;
      case SwipeDirection::Right:
        swipeTitle = "Swipe right area";
        break;
    }
  }
  const char* title = selectorRow_ == kTapZoneSelectorRow
                          ? (tapGridDoubleTap_ ? "Double tap area" : "Tap area")
                          : (selectorRow_ == kSwipeZoneSelectorRow
                                 ? swipeTitle
                                 : buttonLabel(selectorRow_ - kButtonStartRow));
  PopUp::title(renderer, box, title);
  PopUp::list(renderer, box, labels, -1, selectorScroll_);
  PopUp::border(renderer, box);
}

void ButtonMappingActivity::renderTapGrid() {
  SubPage::header(renderer, tapGridDoubleTap_ ? "Double tap zones" : "Tap zones");
  const int font = systemFontId();
  for (int cell = 0; cell < kTapZoneCount; ++cell) {
    const TapGridCellBounds bounds = tapGridCellBounds(renderer, cell);
    if ((cell % ReaderSetting::TAP_ZONE_COLUMNS) != 1) {
      drawTapZoneDither(renderer, bounds);
    }
    renderer.rectangle.render(bounds.x, bounds.y, bounds.width, bounds.height, true);

    const uint8_t action = tapGridDoubleTap_ ? READER_SETTINGS.doubleTapZoneActions[cell]
                                             : READER_SETTINGS.tapZoneActions[cell];
    if (action != SystemSetting::BTN_ACTION_NONE) {
      const char* label = SystemSetting::readerButtonActionLabel(action);
      const int rotatedTextHeight = renderer.text.getLineHeight(font);
      const int rotatedTextWidth = renderer.text.getWidth(font, label);
      const int rotatedTopInset = renderer.text.getFontTopInset(font);
      const int rotatedX = bounds.x + (bounds.width - rotatedTextHeight) / 2 - rotatedTopInset;


      const int rotatedY = bounds.y + (bounds.height + rotatedTextWidth) / 2;
      renderer.text.rotated90CW(font, rotatedX, rotatedY, label, true,
                                EpdFontFamily::REGULAR);
    }
  }
}

void ButtonMappingActivity::renderSwipeGrid() {
  SubPage::header(renderer, "Swipe zones");
  const int font = systemFontId();
  for (int cell = 0; cell < ReaderSetting::SWIPE_ZONE_COLUMNS; ++cell) {
    const TapGridCellBounds bounds = swipeGridCellBounds(renderer, cell);
    if ((cell % ReaderSetting::SWIPE_ZONE_COLUMNS) != 1) drawTapZoneDither(renderer, bounds);
    renderer.line.render(bounds.x, bounds.y, bounds.x, bounds.y + bounds.height - 1, true);
    renderer.line.render(bounds.x + bounds.width - 1, bounds.y, bounds.x + bounds.width - 1,
                         bounds.y + bounds.height - 1, true);
    renderer.line.render(bounds.x, bounds.y, bounds.x + bounds.width - 1, bounds.y, true);
    renderer.line.render(bounds.x, bounds.y + bounds.height - 1, bounds.x + bounds.width - 1,
                         bounds.y + bounds.height - 1, true);
    const bool verticalZone = (cell % ReaderSetting::SWIPE_ZONE_COLUMNS) != 1;
    const bool rightZone = cell == ReaderSetting::SWIPE_ZONE_COLUMNS - 1;
    const int thirdHeight = bounds.height / 3;
    const TapGridCellBounds top = {bounds.x, bounds.y, bounds.width, thirdHeight};
    const TapGridCellBounds middle = {bounds.x, bounds.y + thirdHeight, bounds.width, thirdHeight};
    const TapGridCellBounds bottom = {bounds.x, bounds.y + thirdHeight * 2,
                                      bounds.width, bounds.height - thirdHeight * 2};
    if (verticalZone || rightZone) {
      renderSwipeGestureIndicator(renderer, top, font, SystemSetting::BTN_ACTION_NONE,
                                  READER_SETTINGS.swipeDownZoneActions[cell],
                                  SystemSetting::BTN_ACTION_NONE, SystemSetting::BTN_ACTION_NONE,
                                  nullptr, "Swipe Down", nullptr, nullptr);
      renderSwipeGestureIndicator(renderer, bottom, font, READER_SETTINGS.swipeUpZoneActions[cell],
                                  SystemSetting::BTN_ACTION_NONE,
                                  SystemSetting::BTN_ACTION_NONE, SystemSetting::BTN_ACTION_NONE,
                                  "Swipe Up", nullptr, nullptr, nullptr);
    }
    if (cell == 0) {
      renderSwipeGestureIndicator(renderer, middle, font, SystemSetting::BTN_ACTION_NONE,
                                  SystemSetting::BTN_ACTION_NONE,
                                  SystemSetting::BTN_ACTION_NONE,
                                  READER_SETTINGS.swipeRightZoneActions[cell],
                                  nullptr, nullptr, nullptr, "Swipe Right");
    } else if (rightZone) {
      renderSwipeGestureIndicator(renderer, middle, font, SystemSetting::BTN_ACTION_NONE,
                                  SystemSetting::BTN_ACTION_NONE,
                                  READER_SETTINGS.swipeLeftZoneActions[cell],
                                  SystemSetting::BTN_ACTION_NONE,
                                  nullptr, nullptr, "Swipe Left", nullptr);
    }
  }



  const TapGridCellBounds left = swipeGridCellBounds(renderer, 0);
  const TapGridCellBounds right = swipeGridCellBounds(renderer, ReaderSetting::SWIPE_ZONE_COLUMNS - 1);
  const int firstDividerY = left.y + left.height / 3;
  const int secondDividerY = left.y + (left.height * 2) / 3;
  renderer.line.render(left.x, firstDividerY, right.x + right.width - 1, firstDividerY, true,
                       LineRender::Style::Dotted);
  renderer.line.render(left.x, secondDividerY, right.x + right.width - 1, secondDividerY, true,
                       LineRender::Style::Dotted);
}

void ButtonMappingActivity::renderMain() {
  const int bodyTop = SubPage::header(renderer, "Button & Gestures");
  const int font = systemFontId();
  const int screenW = renderer.getScreenWidth();
  constexpr int rowCount = kMainRowCount;
  constexpr int caretWidth = 32;
  constexpr int caretHeight = 30;
  constexpr int rightInset = 30;

  for (int row = 0; row < rowCount; ++row) {
    const int y = bodyTop + row * kRowHeight;
    const int textY = y + (kRowHeight - renderer.text.getLineHeight(font)) / 2;
    const char* label = row == 0 ? "Quick Actions"
                                 : (row == 1 ? "Buttons"
                                             : (row == 2 ? "Gestures"
                                                         : (row == kMainDisableLightRow ? "Disable light control"
                                                                                         : "Disable highlight")));
    renderer.text.render(font, 20, textY, label, true, EpdFontFamily::REGULAR);
    if (row == kMainDisableLightRow || row == kMainDisableHighlightRow) {
      ReaderFontSettingsDraw::drawToggleCheckbox(
          renderer, screenW - rightInset, y, kRowHeight, false,
          row == kMainDisableLightRow ? READER_SETTINGS.disableLightControl != 0
                                       : READER_SETTINGS.disableHighlight != 0);
    } else {
      const int iconX = screenW - rightInset - caretWidth;
      const int iconY = y + (kRowHeight - caretHeight) / 2;
      renderer.bitmap.icon(LibraryFilterRight, iconX, iconY, caretWidth, caretHeight);
    }
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
    const bool isTapZones = section_ == Section::Gestures && row == 0;
    const bool isDoubleTapZones = section_ == Section::Gestures && row == 1;
    const bool isSwipeZones = section_ == Section::Gestures && row == 2;
    const char* label = section_ == Section::Buttons
                            ? buttonLabel(row)
                            : (isTapZones ? "Tap" : (isDoubleTapZones ? "Double tap" : "Swipe"));
    renderer.text.render(font, 20, textY, label, true, EpdFontFamily::REGULAR);

    const uint8_t* action = actionSlot(mappedRow);
    const char* value = nullptr;
    if (isTapZones || isDoubleTapZones || isSwipeZones) {
      constexpr int caretWidth = 32;
      constexpr int caretHeight = 30;
      const int caretX = valueRight - caretWidth;
      const int caretY = y + (kRowHeight - caretHeight) / 2;
      renderer.bitmap.icon(LibraryFilterRight, caretX, caretY, caretWidth, caretHeight);
    } else {
      value = action ? SystemSetting::readerButtonActionLabel(*action) : "None";
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
  } else if (tapGridOpen_) {
    renderTapGrid();
  } else if (swipeGridOpen_) {
    renderSwipeGrid();
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
  if (tapGridOpen_) {
    handleTapGridInput();
    return;
  }
  if (swipeGridOpen_) {
    handleSwipeGridInput();
    return;
  }
  if (section_ == Section::Main) {
    handleMainInput();
  } else {
    handleListInput();
  }
}

#include "QuickActionsSettingsActivity.h"
#include "system/UiLayout.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "activity/page/SubPage.h"
#include "ReaderFontSettingsDraw.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
constexpr int kRowH = UiLayout::LIST_ITEM_HEIGHT;
constexpr int kValueColumnRight = 30;

/** Actions available in the in-reader popup. */
std::vector<uint8_t> eligibleActions() {
  std::vector<uint8_t> actions;
  for (int i = 1; i < static_cast<int>(SystemSetting::READER_BUTTON_ACTION_COUNT); ++i) {
    if (i == SystemSetting::BTN_ACTION_QUICK_ACTIONS || i == SystemSetting::BTN_ACTION_BOOKMARK ||
        i == SystemSetting::BTN_ACTION_TABLE_OF_CONTENTS || i == SystemSetting::BTN_ACTION_OPEN_SETTINGS) {
      continue;
    }
    actions.push_back(static_cast<uint8_t>(i));
  }
  std::sort(actions.begin(), actions.end(), [](const uint8_t a, const uint8_t b) {
    return strcmp(SystemSetting::readerButtonActionLabel(a), SystemSetting::readerButtonActionLabel(b)) < 0;
  });
  return actions;
}
}  // namespace

void QuickActionsSettingsActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  selectedIndex_ = 0;
  scrollOffset_ = 0;
  render();
}

void QuickActionsSettingsActivity::toggleSelected() {
  const std::vector<uint8_t> actions = eligibleActions();
  if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(actions.size())) {
    return;
  }
  const uint32_t bit = 1u << actions[static_cast<size_t>(selectedIndex_)];
  READER_SETTINGS.quickActionsMask ^= bit;
  READER_SETTINGS.saveToFile();
}

void QuickActionsSettingsActivity::loop() {
  if (SubPage::closeInput(renderer, mappedInput, onDone_)) return;

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int screenH = renderer.getScreenHeight();
      const int bodyTop = UiLayout::PAGE_HEADER_HEIGHT;
      const int visibleRows = std::max(1, (screenH - 44 - bodyTop) / kRowH);
      const int tapY = static_cast<int>(tapNy * screenH);
      if (tapY >= bodyTop && tapY < bodyTop + visibleRows * kRowH) {
        const int tappedIndex = scrollOffset_ + (tapY - bodyTop) / kRowH;
        if (tappedIndex >= 0 && tappedIndex < static_cast<int>(eligibleActions().size())) {
          selectedIndex_ = tappedIndex;
          toggleSelected();
          render();
          return;
        }
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onDone_();
    return;
  }

  const std::vector<uint8_t> actions = eligibleActions();
  const int total = static_cast<int>(actions.size());
  if (total == 0) {
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedIndex_ = (selectedIndex_ + 1) % total;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedIndex_ = (selectedIndex_ + total - 1) % total;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    toggleSelected();
    render();
    return;
  }
}

void QuickActionsSettingsActivity::render() {
  renderer.clearScreen();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int bodyTop = SubPage::header(renderer, "Quick Actions");

  const std::vector<uint8_t> actions = eligibleActions();
  const int total = static_cast<int>(actions.size());
  if (total == 0) {
    const int centerY = bodyTop + (screenH - bodyTop - 80) / 2;
    renderer.text.centered(systemFontId(), centerY, "No actions available.", true, EpdFontFamily::BOLD);
    const auto hints = mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
    renderer.displayBuffer();
    return;
  }

  const int listBottom = screenH - 44;
  const int visibleRows = std::max(1, (listBottom - bodyTop) / kRowH);
  if (selectedIndex_ < scrollOffset_) {
    scrollOffset_ = selectedIndex_;
  } else if (selectedIndex_ >= scrollOffset_ + visibleRows) {
    scrollOffset_ = selectedIndex_ - visibleRows + 1;
  }
  const int maxScroll = std::max(0, total - visibleRows);
  scrollOffset_ = std::max(0, std::min(scrollOffset_, maxScroll));
  const int endIndex = std::min(total, scrollOffset_ + visibleRows);

  for (int i = scrollOffset_; i < endIndex; ++i) {
    const int y = bodyTop + (i - scrollOffset_) * kRowH;
    const uint8_t action = actions[static_cast<size_t>(i)];
    const bool checked = (READER_SETTINGS.quickActionsMask & (1u << action)) != 0;
    const int font = systemFontId();
    const int titleY = y + (kRowH - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, 20, titleY, SystemSetting::readerButtonActionLabel(action), true,
                         EpdFontFamily::REGULAR);
    ReaderFontSettingsDraw::drawToggleCheckbox(renderer, screenW - kValueColumnRight, y, kRowH, false, checked);
    renderer.line.render(0, y + kRowH - 1, screenW, y + kRowH - 1, true, LineRender::Style::Dotted);
  }

  const auto hints = mappedInput.mapLabels("\xC2\xAB Back", "Toggle", "Up", "Down");
  renderer.displayBuffer();
}

#include "QuickActionsSettingsActivity.h"
#include "system/UiLayout.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Button.h"
#include "ReaderFontSettingsDraw.h"
#include "images/LibraryFilterRight.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
constexpr int kRowH = UiLayout::LIST_ITEM_HEIGHT;
constexpr int kSideMargin = 20;
constexpr int kBottomMargin = 44;
constexpr int kScrollCaretSize = 40;
constexpr int kValueColumnRight = 30;

int pageBodyTop() { return FREEINK_DEVICE_X4PRO ? 80 : 70; }

ButtonBounds scrollCaretBounds(const GfxRenderer& renderer) {
  return {renderer.getScreenWidth() - kSideMargin - kScrollCaretSize,
          renderer.getScreenHeight() - kBottomMargin + 2, kScrollCaretSize, kScrollCaretSize};
}

bool contains(const ButtonBounds& bounds, const int x, const int y) {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}

void drawScrollBar(const GfxRenderer& renderer, const int x, const int y, const int height, const int total,
                   const int visible, const int offset) {
  if (total <= visible || height <= 0) return;

  constexpr int width = 3;
  const int maxOffset = std::max(1, total - visible);
  const int thumbHeight = std::max(14, height * visible / total);
  const int thumbTravel = std::max(1, height - thumbHeight);
  const int thumbY = y + offset * thumbTravel / maxOffset;
  renderer.rectangle.fill(x, y, width, height, static_cast<int>(GfxRenderer::FillTone::Gray), true);
  renderer.rectangle.fill(x, thumbY, width, thumbHeight, static_cast<int>(GfxRenderer::FillTone::Ink), true);
}

bool isSupportedReaderAction(const int action) {
#if FREEINK_DEVICE_X4PRO
  return true;
#else
  return action != SystemSetting::BTN_ACTION_TOGGLE_LIGHT;
#endif
}

/** Actions available in the in-reader popup. */
std::vector<uint8_t> eligibleActions() {
  std::vector<uint8_t> actions;
  for (int i = 1; i < static_cast<int>(SystemSetting::READER_BUTTON_ACTION_COUNT); ++i) {
    if (i == SystemSetting::BTN_ACTION_QUICK_ACTIONS || i == SystemSetting::BTN_ACTION_BOOKMARK ||
        i == SystemSetting::BTN_ACTION_TABLE_OF_CONTENTS || i == SystemSetting::BTN_ACTION_OPEN_SETTINGS) {
      continue;
    }
    if (!isSupportedReaderAction(i)) continue;
    actions.push_back(static_cast<uint8_t>(i));
  }
  std::sort(actions.begin(), actions.end(), [](const uint8_t a, const uint8_t b) {
    return strcmp(SystemSetting::readerButtonActionLabel(a), SystemSetting::readerButtonActionLabel(b)) < 0;
  });
  return actions;
}
}

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
  if (SubPage::closeInput(renderer, mappedInput, onDone_, false)) return;

  const std::vector<uint8_t> actions = eligibleActions();
  const int total = static_cast<int>(actions.size());
  const int bodyTop = pageBodyTop();
  const int visibleRows = std::max(1, (renderer.getScreenHeight() - kBottomMargin - bodyTop) / kRowH);
  const int maxScroll = std::max(0, total - visibleRows);

  if (mappedInput.hasTouch()) {
    if (mappedInput.wasTouchSwipeUpForRenderer(renderer)) {
      scrollOffset_ = std::min(maxScroll, scrollOffset_ + std::max(1, visibleRows - 1));
      selectedIndex_ = std::min(std::max(0, total - 1), scrollOffset_);
      render();
      return;
    }
    if (mappedInput.wasTouchSwipeDownForRenderer(renderer)) {
      scrollOffset_ = std::max(0, scrollOffset_ - std::max(1, visibleRows - 1));
      selectedIndex_ = std::min(std::max(0, total - 1), scrollOffset_);
      render();
      return;
    }
  }

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int screenH = renderer.getScreenHeight();
      const int tapY = static_cast<int>(tapNy * screenH);
      const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
      if (maxScroll > 0 && contains(scrollCaretBounds(renderer), tapX, tapY)) {
        if (scrollOffset_ >= maxScroll) {
          scrollOffset_ = std::max(0, scrollOffset_ - std::max(1, visibleRows - 1));
        } else {
          scrollOffset_ = std::min(maxScroll, scrollOffset_ + std::max(1, visibleRows - 1));
        }
        selectedIndex_ = std::min(std::max(0, total - 1), scrollOffset_);
        render();
        return;
      }
      if (tapY >= bodyTop && tapY < bodyTop + visibleRows * kRowH) {
        const int tappedIndex = scrollOffset_ + (tapY - bodyTop) / kRowH;
        if (tappedIndex >= 0 && tappedIndex < total) {
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

  const int listBottom = screenH - kBottomMargin;
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
    renderer.text.render(font, kSideMargin, titleY, SystemSetting::readerButtonActionLabel(action), true,
                         EpdFontFamily::REGULAR);
    ReaderFontSettingsDraw::drawToggleCheckbox(renderer, screenW - kValueColumnRight, y, kRowH, false, checked);
    if (i + 1 < endIndex) {
      renderer.line.render(0, y + kRowH - 1, screenW, y + kRowH - 1, true, LineRender::Style::Dotted);
    }
  }

  drawScrollBar(renderer, screenW - 8, bodyTop, visibleRows * kRowH, total, visibleRows, scrollOffset_);
  const int maxScrollForCaret = std::max(0, total - visibleRows);
  const auto caretOrientation = scrollOffset_ >= maxScrollForCaret ? BitmapRender::Orientation::Rotate270CW
                                                                     : BitmapRender::Orientation::Rotate90CW;
  const ButtonBounds caretBounds = scrollCaretBounds(renderer);
  renderer.bitmap.iconScaled(LibraryFilterRight, caretBounds.x, caretBounds.y, 30, 30, kScrollCaretSize,
                             kScrollCaretSize, caretOrientation);

  const auto hints = mappedInput.mapLabels("\xC2\xAB Back", "Toggle", "Up", "Down");
  renderer.displayBuffer();
}

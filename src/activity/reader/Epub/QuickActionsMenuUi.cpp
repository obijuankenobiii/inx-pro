/**
 * @file QuickActionsMenuUi.cpp
 * @brief Definitions for QuickActionsMenuUi.
 */

#include "QuickActionsMenuUi.h"
#include "system/UiLayout.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cstring>

#include "EpubActivity.h"
#include "ReaderButtonBindings.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
constexpr int kVisibleRows = 6;
constexpr int kRowHeight = UiLayout::LIST_ITEM_HEIGHT - 4;
constexpr int kOverlayHeaderHeight = UiLayout::LIST_ITEM_HEIGHT - 4;

struct QuickActionsBounds {
  int x;
  int y;
  int width;
  int height;
  int header;
  int row;
  int rows;
};

QuickActionsBounds popupBounds(const GfxRenderer& renderer, const int count) {
  const int rows = std::min(kVisibleRows, count);
  const int boxW = std::min(renderer.getScreenWidth() - 60, 320);
  const int boxH = kOverlayHeaderHeight + rows * kRowHeight;
  return {(renderer.getScreenWidth() - boxW) / 2,
          (renderer.getScreenHeight() - boxH) / 2,
          boxW,
          boxH,
          kOverlayHeaderHeight,
          kRowHeight,
          rows};
}
}

void QuickActionsMenuUi::enter(EpubActivity& act) {
  actions_.clear();
  for (int i = 1; i < static_cast<int>(SystemSetting::READER_BUTTON_ACTION_COUNT); ++i) {
    if (i == SystemSetting::BTN_ACTION_QUICK_ACTIONS || i == SystemSetting::BTN_ACTION_BOOKMARK ||
        i == SystemSetting::BTN_ACTION_TABLE_OF_CONTENTS || i == SystemSetting::BTN_ACTION_OPEN_SETTINGS) {
      continue;
    }
    if (READER_SETTINGS.quickActionsMask & (1u << i)) {
      actions_.push_back(static_cast<uint8_t>(i));
    }
  }
  // A-Z by label, same order as the QuickActionsSettingsActivity checklist this is built from.
  std::sort(actions_.begin(), actions_.end(), [](const uint8_t a, const uint8_t b) {
    return strcmp(SystemSetting::readerButtonActionLabel(a), SystemSetting::readerButtonActionLabel(b)) < 0;
  });
  if (actions_.empty()) {
    act.readerPopup("No quick actions configured");
    return;
  }
  mode_ = true;
  selected_ = 0;
  scroll_ = 0;
  clampScroll();
  render(act);
}

void QuickActionsMenuUi::handleInput(EpubActivity& act) {
  const MappedInputManager& m = act.mappedInput;
  const int count = static_cast<int>(actions_.size());
  if (count == 0) {
    mode_ = false;
    act.renderScreen(true);
    return;
  }

  if (m.wasReleased(MappedInputManager::Button::Back)) {
    mode_ = false;
    act.renderScreen(true);
    return;
  }

  if (m.wasTouchSwipeUpForRenderer(act.renderer) || m.wasTouchSwipeDownForRenderer(act.renderer)) {
    const int rows = std::min(kVisibleRows, count);
    const int maxScroll = std::max(0, count - rows);
    if (m.wasTouchSwipeUpForRenderer(act.renderer)) {
      scroll_ = std::min(scroll_ + rows, maxScroll);
    } else {
      scroll_ = std::max(scroll_ - rows, 0);
    }
    selected_ = scroll_;
    render(act);
    return;
  }

  if (m.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (m.wasTouchTapInScreen(act.renderer, tapNx, tapNy)) {
      const QuickActionsBounds box = popupBounds(act.renderer, count);
      const int tapX = static_cast<int>(tapNx * act.renderer.getScreenWidth());
      const int tapY = static_cast<int>(tapNy * act.renderer.getScreenHeight());
      const int optionY = tapY - box.y - box.header;
      if (tapX >= box.x && tapX < box.x + box.width && optionY >= 0 && optionY < box.rows * box.row) {
        const int option = scroll_ + optionY / box.row;
        if (option >= 0 && option < count) {
          const uint8_t chosen = actions_[static_cast<size_t>(option)];
          mode_ = false;
          act.renderScreen(true);
          act.btnBindings_.dispatch(act, chosen);
          return;
        }
      }
      mode_ = false;
      act.renderScreen(true);
      return;
    }
  }

  if (m.wasReleased(MappedInputManager::Button::Confirm)) {
    const uint8_t chosen = actions_[static_cast<size_t>(selected_)];
    mode_ = false;
    act.renderScreen(true);
    act.btnBindings_.dispatch(act, chosen);
    return;
  }

  if (m.wasPressed(MappedInputManager::Button::Up)) {
    selected_ = (selected_ - 1 + count) % count;
    if (selected_ < scroll_) {
      scroll_ = selected_;
    }
    if (selected_ >= scroll_ + kVisibleRows) {
      scroll_ = selected_ - kVisibleRows + 1;
    }
    clampScroll();
    render(act);
    return;
  }

  if (m.wasPressed(MappedInputManager::Button::Down)) {
    selected_ = (selected_ + 1) % count;
    if (selected_ < scroll_) {
      scroll_ = selected_;
    }
    if (selected_ >= scroll_ + kVisibleRows) {
      scroll_ = selected_ - kVisibleRows + 1;
    }
    clampScroll();
    render(act);
    return;
  }
}

void QuickActionsMenuUi::clampScroll() {
  const int count = std::max(1, static_cast<int>(actions_.size()));
  const int rows = std::min(kVisibleRows, count);
  const int maxScroll = std::max(0, count - rows);
  scroll_ = std::max(0, std::min(scroll_, maxScroll));
}

void QuickActionsMenuUi::render(EpubActivity& act) {
  GfxRenderer& renderer = act.renderer;
  // The menu is an overlay over the page currently on the panel. Rebase the
  // writable buffer before painting so X4 Pro cannot swap the prior page in.
  renderer.syncWriteBufferFromActive();
  const int count = static_cast<int>(actions_.size());
  const QuickActionsBounds box = popupBounds(renderer, count);
  const int rows = box.rows;

  renderer.rectangle.fill(box.x, box.y, box.width, box.height, false);

  const int titleY = box.y + (box.header - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;
  renderer.text.render(MONTSERRAT_10_FONT_ID, box.x + 16, titleY, "Quick Actions", true,
                       EpdFontFamily::BOLD);

  clampScroll();
  for (int i = 0; i < rows; ++i) {
    const int actionIdx = scroll_ + i;
    if (actionIdx >= count) {
      break;
    }
    const int rowY = box.y + box.header + i * box.row;

    const char* label = SystemSetting::readerButtonActionLabel(actions_[static_cast<size_t>(actionIdx)]);
    const int textY = rowY + (box.row - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;
    renderer.text.render(MONTSERRAT_10_FONT_ID, box.x + 20, textY, label, true);
    if (i + 1 < rows) {
      renderer.line.render(box.x, rowY + box.row, box.x + box.width, rowY + box.row, true,
                           LineRender::Style::Dotted);
    }
  }

  if (count > rows) {
    const int maxScroll = std::max(1, count - rows);
    const int trackX = box.x + box.width - 10;
    const int trackY = box.y + box.header;
    const int trackH = rows * box.row;
    const int thumbH = std::max(8, trackH * rows / count);
    const int thumbY = trackY + scroll_ * std::max(1, trackH - thumbH) / maxScroll;
    renderer.rectangle.fill(trackX, trackY, 2, trackH, true);
    renderer.rectangle.fill(trackX - 2, thumbY, 6, thumbH, true);
  }

  renderer.line.render(box.x, box.y + box.header, box.x + box.width, box.y + box.header, true);
  renderer.rectangle.render(box.x, box.y, box.width, box.height, true);
  renderer.rectangle.render(box.x + 1, box.y + 1, box.width - 2, box.height - 2, true);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

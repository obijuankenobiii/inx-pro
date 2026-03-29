/**
 * @file GoToPercentUi.cpp
 * @brief Definitions for the in-book percentage navigation popup.
 */

#include "GoToPercentUi.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cstdio>

#include "EpubActivity.h"
#include "images/LibraryFilterLeft.h"
#include "images/LibraryFilterRight.h"
#include "system/Fonts.h"

namespace {
constexpr int kPopupWidth = 420;
constexpr int kPopupHeight = 170;
constexpr int kTrackMargin = 66;
constexpr int kCaretSize = 30;
constexpr int kCaretGap = 8;
constexpr int kCaretTouchPadding = 10;
constexpr int kTrackHeight = 4;
constexpr int kKnobRadius = 22;

struct PopupBounds {
  int x;
  int y;
  int width;
  int height;
  int trackLeft;
  int trackRight;
  int trackY;
};

PopupBounds popupBounds(const GfxRenderer& renderer) {
  const int width = std::min(kPopupWidth, std::max(1, renderer.getScreenWidth() - 30));
  const int height = std::min(kPopupHeight, std::max(1, renderer.getScreenHeight() - 30));
  const int x = (renderer.getScreenWidth() - width) / 2;
  const int y = (renderer.getScreenHeight() - height) / 2;
  const int trackLeft = x + std::min(kTrackMargin, width / 4);
  const int trackRight = x + width - std::min(kTrackMargin, width / 4);
  return {x, y, width, height, trackLeft, trackRight, y + height - 46};
}

int percentFromX(const PopupBounds& bounds, const int x) {
  const int width = std::max(1, bounds.trackRight - bounds.trackLeft);
  const int clamped = std::max(bounds.trackLeft, std::min(bounds.trackRight, x));
  return ((clamped - bounds.trackLeft) * 100 + width / 2) / width;
}

bool pointInPopup(const PopupBounds& bounds, const int x, const int y) {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}

bool pointNearTrack(const PopupBounds& bounds, const int x, const int y) {
  return x >= bounds.trackLeft - kCaretSize - kCaretGap &&
         x < bounds.trackRight + kCaretSize + kCaretGap &&
         y >= bounds.trackY - kKnobRadius - 12 && y < bounds.trackY + kKnobRadius + 12;
}

bool pointInCaret(const PopupBounds& bounds, const int x, const int y, const bool right) {
  const int caretX = right ? bounds.trackRight + kCaretGap : bounds.trackLeft - kCaretGap - kCaretSize;
  return x >= caretX - kCaretTouchPadding && x < caretX + kCaretSize + kCaretTouchPadding &&
         y >= bounds.trackY - kCaretSize / 2 - kCaretTouchPadding &&
         y < bounds.trackY + kCaretSize / 2 + kCaretTouchPadding;
}
}  // namespace

void GoToPercentUi::enter(EpubActivity& act) {
  percent_ = 0;
  if (act.epub && act.section && act.section->pageCount > 0) {
    const float spineProgress = static_cast<float>(act.section->currentPage) /
                                static_cast<float>(act.section->pageCount);
    const float bookProgress = act.epub->calculateProgress(act.currentSpineIndex, spineProgress);
    percent_ = std::max(0, std::min(100, static_cast<int>(bookProgress * 100.0f + 0.5f)));
  }
  active_ = true;
  dragging_ = false;
  dragChanged_ = false;
  caretPressed_ = false;
  render(act);
}

void GoToPercentUi::handleInput(EpubActivity& act) {
  if (!active_) return;

  const MappedInputManager& input = act.mappedInput;
  const auto closeAndCommit = [&]() {
    if (dragChanged_) {
      act.jumpToPercent(percent_);
      act.updateRequired = true;
      act.startPageTimer();
    } else {
      act.renderScreen(true);
    }
    active_ = false;
    dragging_ = false;
    dragChanged_ = false;
    caretPressed_ = false;
  };

  if (input.wasReleased(MappedInputManager::Button::Back)) {
    closeAndCommit();
    return;
  }

  const PopupBounds bounds = popupBounds(act.renderer);
  const auto updateFromTouch = [&](const int x) {
    const int next = percentFromX(bounds, x);
    if (next == percent_) return false;
    percent_ = next;
    dragChanged_ = true;
    return true;
  };

  if (caretPressed_) {
    if (input.isTouchPressed()) return;
    caretPressed_ = false;
    return;
  }

  if (dragging_) {
    if (input.isTouchPressed()) {
      float nx = 0.0f;
      float ny = 0.0f;
      if (input.isTouchHeldInScreen(act.renderer, nx, ny) &&
          updateFromTouch(static_cast<int>(nx * act.renderer.getScreenWidth()))) {
        render(act);
      }
      return;
    }

    // Keep the popup open after release. The user can adjust the marker again;
    // navigation is committed only when the popup is closed.
    dragging_ = false;
    return;
  }

  float nx = 0.0f;
  float ny = 0.0f;
  if (input.wasTouchPressedInScreen(act.renderer, nx, ny)) {
    const int x = static_cast<int>(nx * act.renderer.getScreenWidth());
    const int y = static_cast<int>(ny * act.renderer.getScreenHeight());
    if (pointInPopup(bounds, x, y) && pointInCaret(bounds, x, y, false)) {
      percent_ = std::max(0, percent_ - 1);
      dragChanged_ = true;
      caretPressed_ = true;
      render(act);
      return;
    }
    if (pointInPopup(bounds, x, y) && pointInCaret(bounds, x, y, true)) {
      percent_ = std::min(100, percent_ + 1);
      dragChanged_ = true;
      caretPressed_ = true;
      render(act);
      return;
    }
    if (pointInPopup(bounds, x, y) && pointNearTrack(bounds, x, y)) {
      dragging_ = true;
      dragChanged_ = updateFromTouch(x);
      render(act);
      return;
    }
    if (pointInPopup(bounds, x, y)) {
      return;
    }
    return;
  }

  if (input.wasTouchTapInScreen(act.renderer, nx, ny)) {
    const int x = static_cast<int>(nx * act.renderer.getScreenWidth());
    const int y = static_cast<int>(ny * act.renderer.getScreenHeight());
    if (pointInPopup(bounds, x, y) && pointNearTrack(bounds, x, y)) {
      if (updateFromTouch(x)) render(act);
      return;
    }
    closeAndCommit();
    return;
  }
}

void GoToPercentUi::render(EpubActivity& act) {
  GfxRenderer& renderer = act.renderer;
  renderer.syncWriteBufferFromActive();
  const PopupBounds bounds = popupBounds(renderer);
  renderer.rectangle.fill(bounds.x, bounds.y, bounds.width, bounds.height, false);

  const int titleFont = systemFontId();
  const int titleHeight = renderer.text.getLineHeight(titleFont);
  renderer.text.centered(titleFont, bounds.y + 25 + titleHeight / 2, "Go to Percent", true,
                         EpdFontFamily::BOLD);

  renderer.bitmap.icon(LibraryFilterLeft, bounds.trackLeft - kCaretGap - kCaretSize,
                       bounds.trackY - kCaretSize / 2, kCaretSize, kCaretSize);
  renderer.bitmap.icon(LibraryFilterRight, bounds.trackRight + kCaretGap,
                       bounds.trackY - kCaretSize / 2, kCaretSize, kCaretSize);
  renderer.rectangle.fill(bounds.trackLeft, bounds.trackY - kTrackHeight / 2,
                          bounds.trackRight - bounds.trackLeft, kTrackHeight, true, true);

  const int knobX = bounds.trackLeft + (bounds.trackRight - bounds.trackLeft) * percent_ / 100;
  renderer.circle.render(knobX, bounds.trackY, kKnobRadius, true);
  renderer.circle.render(knobX, bounds.trackY, kKnobRadius - 2, false);

  char percentText[6];
  std::snprintf(percentText, sizeof(percentText), "%d%%", percent_);
  const int textWidth = renderer.text.getWidth(MONTSERRAT_8_FONT_ID, percentText, EpdFontFamily::BOLD);
  const int textHeight = renderer.text.getLineHeight(MONTSERRAT_8_FONT_ID);
  renderer.text.render(MONTSERRAT_8_FONT_ID, knobX - textWidth / 2, bounds.trackY - textHeight / 2,
                       percentText, true, EpdFontFamily::BOLD);

  renderer.rectangle.render(bounds.x, bounds.y, bounds.width, bounds.height, true);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  renderer.syncWriteBufferFromActive();
}

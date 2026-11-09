/**
 * @file SubPage.cpp
 * @brief Shared title, close button, and touch behavior for sub-pages.
 */

#include "SubPage.h"

#include <GfxRenderer.h>

#include "images/Close.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
constexpr float kBottomEdgeSwipeStartNy = 0.95f;
}

void SubPage::closeIcon(const GfxRenderer& renderer, const int x, const int y, const int size) {
  constexpr int sourceSize = 40;
  renderer.bitmap.iconScaled(Close, x, y, sourceSize, sourceSize, size, size);
}

int SubPage::header(const GfxRenderer& renderer, const char* name) {
  constexpr int top = navigation::Menu::topPadding;
  constexpr int size = navigation::Menu::iconSize;
  const int font = systemFontIdForPointSize(16);
  const int textY = top + (size - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, navigation::Menu::leftMargin, textY, name ? name : "", true,
                       EpdFontFamily::BOLD);
  closeIcon(renderer, renderer.getScreenWidth() - 60, top, size);
  return top + size + 20;
}

int SubPage::header(const GfxRenderer& renderer, const char* name, const char* mode) {
  const int contentTop = header(renderer, name);
  if (!mode || mode[0] == '\0') return contentTop;

  constexpr int top = navigation::Menu::topPadding;
  constexpr int size = navigation::Menu::iconSize;
  const int titleFont = systemFontIdForPointSize(16);
  const int modeFont = systemFontIdForPointSize(12);
  const int titleWidth = renderer.text.getWidth(titleFont, name ? name : "", EpdFontFamily::BOLD);
  const int modeY = top + (size - renderer.text.getLineHeight(modeFont)) / 2;
  renderer.text.renderGray(modeFont, navigation::Menu::leftMargin + titleWidth + 10, modeY, mode, true,
                           EpdFontFamily::BOLD);
  return contentTop;
}

bool SubPage::closeInput(GfxRenderer& renderer, MappedInputManager& mappedInput,
                         const std::function<void()>& close, const bool closeOnSwipeUp) {
  if (closeOnSwipeUp && mappedInput.hasTouch()) {
    float swipeStartNx = 0.0f;
    float swipeStartNy = 0.0f;
    if (mappedInput.wasTouchSwipeUpInScreen(renderer, swipeStartNx, swipeStartNy) &&
        swipeStartNy >= kBottomEdgeSwipeStartNy) {
      if (close) close();
      return true;
    }
  }

  if (mappedInput.hasTouch()) {
    float startX = 0.0f;
    float startY = 0.0f;
    if (mappedInput.wasTouchSwipeRightInScreen(renderer, startX, startY) && startX <= 0.30f) {
      if (close) close();
      return true;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (close) close();
    return true;
  }

  if (!mappedInput.hasTouch()) return false;

  float tapX = 0.0f;
  float tapY = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) return false;

  const int x = static_cast<int>(tapX * renderer.getScreenWidth());
  const int y = static_cast<int>(tapY * renderer.getScreenHeight());
  constexpr int headerTop = navigation::Menu::topPadding;
  constexpr int closeSize = navigation::Menu::iconSize;
  constexpr int closeTouchPadding = 10;
  const int closeLeft = renderer.getScreenWidth() - 60 - closeTouchPadding;
  const int closeRight = renderer.getScreenWidth() - 20 + closeTouchPadding;
  const int closeTop = headerTop - closeTouchPadding;
  const int closeBottom = headerTop + closeSize + closeTouchPadding;
  if (x >= closeLeft && x < closeRight && y >= closeTop && y < closeBottom) {
    if (close) close();
    return true;
  }

  mappedInput.restoreTouchTapInScreen(renderer, tapX, tapY);
  return false;
}

SubPage::SubPage(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                 std::function<void()> close)
    : UiPage(name, renderer, mappedInput), close(std::move(close)) {}

void SubPage::menu() {
  title();
  closeIcon(renderer, renderer.getScreenWidth() - 60, 20, 40);
}

bool SubPage::closeInput(const bool closeOnSwipeUp) {
  if (closeOnSwipeUp && mappedInput.hasTouch()) {
    float swipeStartNx = 0.0f;
    float swipeStartNy = 0.0f;
    if (mappedInput.wasTouchSwipeUpInScreen(renderer, swipeStartNx, swipeStartNy) &&
        swipeStartNy >= kBottomEdgeSwipeStartNy) {
      dismiss();
      return true;
    }
  }

  if (mappedInput.hasTouch()) {
    float startX = 0.0f;
    float startY = 0.0f;
    if (mappedInput.wasTouchSwipeRightInScreen(renderer, startX, startY) && startX <= 0.30f) {
      dismiss();
      return true;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    dismiss();
    return true;
  }

  if (!mappedInput.hasTouch()) return false;

  float tapX = 0.0f;
  float tapY = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) return false;

  const int x = static_cast<int>(tapX * renderer.getScreenWidth());
  const int y = static_cast<int>(tapY * renderer.getScreenHeight());
  if (x >= renderer.getScreenWidth() - 60 && x < renderer.getScreenWidth() - 20 && y >= 20 && y < 60) {
    dismiss();
    return true;
  }

  mappedInput.restoreTouchTapInScreen(renderer, tapX, tapY);
  return false;
}

void SubPage::dismiss() {
  if (close) close();
}

void SubPage::loop() {
  if (closeInput()) return;
  renderPage();
}

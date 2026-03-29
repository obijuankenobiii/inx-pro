/**
 * @file SubPage.cpp
 * @brief Shared title, close button, and touch behavior for sub-pages.
 */

#include "SubPage.h"

#include <GfxRenderer.h>

#include "images/Close.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

int SubPage::header(const GfxRenderer& renderer, const char* name) {
  constexpr int top = FREEINK_DEVICE_X4PRO ? 20 : 10;
  constexpr int size = 40;
  const int font = MONTSERRAT_16_FONT_ID;
  const int textY = top + (size - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, 20, textY, name ? name : "", true, EpdFontFamily::BOLD);
  renderer.bitmap.icon(Close, renderer.getScreenWidth() - 60, top, size, size);
  return top + size + 20;
}

bool SubPage::closeInput(GfxRenderer& renderer, MappedInputManager& mappedInput,
                         const std::function<void()>& close, const bool closeOnSwipeUp) {
  if (closeOnSwipeUp && mappedInput.hasTouch() && mappedInput.wasTouchSwipeUp()) {
    if (close) close();
    return true;
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
  if (x >= renderer.getScreenWidth() - 60 && x < renderer.getScreenWidth() - 20 && y >= 20 && y < 60) {
    if (close) close();
    return true;
  }

  mappedInput.restoreTouchTapInScreen(renderer, tapX, tapY);
  return false;
}

SubPage::SubPage(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                 std::function<void()> close)
    : Page(name, renderer, mappedInput), close(std::move(close)) {}

void SubPage::menu() {
  title();
  renderer.bitmap.icon(Close, renderer.getScreenWidth() - 60, 20, 40, 40);
}

bool SubPage::closeInput(const bool closeOnSwipeUp) {
  if (closeOnSwipeUp && mappedInput.hasTouch() && mappedInput.wasTouchSwipeUp()) {
    dismiss();
    return true;
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

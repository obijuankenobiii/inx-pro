/**
 * @file Page.cpp
 * @brief Minimal page shell for the new page redesign.
 */

#include "Page.h"

#include <GfxRenderer.h>

#include "system/MappedInputManager.h"
#include "util/LibraryIndexRefresh.h"

extern void onGoToHome();
extern void onGoToLibrary(const std::string& path);
extern void onGoToFileTransfer();
extern void onGoToSettings();
extern void onGoToStatistics();
extern void openSearchFromCallback(std::function<void()> returnToCaller);

Page::Page(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(name, renderer, mappedInput), navigation::Menu(renderer) {}

void Page::onEnter() {
  Activity::onEnter();
  updateRequired = true;
}

void Page::loop() {
  if (menuInput()) return;
  renderPage();
}

bool Page::menuInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    return back();
  }

  if (mappedInput.wasTouchSwipeUpForRenderer(renderer) || mappedInput.wasTouchSwipeDownForRenderer(renderer) ||
      mappedInput.wasTouchSwipeLeftForRenderer(renderer) || mappedInput.wasTouchSwipeRightForRenderer(renderer)) {
    const navigation::Menu::Action action = navigation::Menu::handleInput(mappedInput);
    if (action == navigation::Menu::Action::None) return false;
    routeMenuAction(action);
    return true;
  }

#if FREEINK_DEVICE_X4PRO
  if (mappedInput.isTouchPressed() || isLightDrawerSliderDragging()) {
    const navigation::Menu::Action action = navigation::Menu::handleInput(mappedInput);
    if (action != navigation::Menu::Action::None) {
      routeMenuAction(action);
      return true;
    }
    if (isLightDrawerSliderDragging()) return true;
  }
#endif

  if (mappedInput.hasTouch()) {

    float tapX = 0.0f;
    float tapY = 0.0f;
    if (!mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) return false;

    const int x = static_cast<int>(tapX * renderer.getScreenWidth());
    const int y = static_cast<int>(tapY * renderer.getScreenHeight());
    const navigation::Menu::Action action = navigation::Menu::handleTap(x, y);
    if (action == navigation::Menu::Action::None) {
      mappedInput.restoreTouchTapInScreen(renderer, tapX, tapY);
      return false;
    }

    routeMenuAction(action);
    return true;
  }

  const navigation::Menu::Action action = navigation::Menu::handleInput(mappedInput);
  if (action == navigation::Menu::Action::None) return false;
  routeMenuAction(action);
  return true;
}

void Page::renderPage() {
  if (!updateRequired) return;
  render();
  updateRequired = false;
}

bool Page::routeMenuAction(const navigation::Menu::Action action) {
  switch (action) {
    case navigation::Menu::Action::Opened:
    case navigation::Menu::Action::Closed:
      updateRequired = true;
      break;
    case navigation::Menu::Action::Search:
      search();
      return true;
    case navigation::Menu::Action::Refresh:
      refresh();
      return true;
    case navigation::Menu::Action::View:
    case navigation::Menu::Action::Sort:
    case navigation::Menu::Action::Filter:
      return menuAction(action);
    case navigation::Menu::Action::Home:
      onGoToHome();
      return true;
    case navigation::Menu::Action::Library:
      onGoToLibrary("/");
      return true;
    case navigation::Menu::Action::Settings:
      onGoToSettings();
      return true;
    case navigation::Menu::Action::Stats:
      onGoToStatistics();
      return true;
    case navigation::Menu::Action::Sync:
      onGoToFileTransfer();
      return true;
    case navigation::Menu::Action::None:
    default:
      break;
  }
  return false;
}

void Page::menu() { navigation::Menu::render(); }

void Page::refresh() { LibraryIndexRefresh::start(renderer, this); }

void Page::search() { openSearchFromCallback([] { onGoToHome(); }); }

bool Page::back() {
  onGoToHome();
  return true;
}

bool Page::menuAction(const navigation::Menu::Action action) {
  (void)action;
  return false;
}

void Page::render() {
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  content();
  menu();
  renderer.displayBufferAsync();
}

int Page::contentBottomY(const GfxRenderer& renderer) {
  return renderer.getScreenHeight() - TAB_BAR_HEIGHT - CONTENT_BOTTOM_PADDING;
}

int Page::tabBarTopY(const GfxRenderer& renderer) { return renderer.getScreenHeight() - TAB_BAR_HEIGHT; }

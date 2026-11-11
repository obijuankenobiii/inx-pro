/**
 * @file UiPage.cpp
 * @brief Minimal page shell for the new page redesign.
 */

#include "Page.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include "system/MappedInputManager.h"
#include "util/LibraryIndexRefresh.h"

extern void onGoToHome();
extern void onGoToLibrary(const std::string& path);
extern void onGoToFileTransfer();
extern void onGoToSettings();
extern void onGoToStatistics();
extern void onGoToApps();
extern void openSearchFromCallback(std::function<void()> returnToCaller);

UiPage::UiPage(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(name, renderer, mappedInput), navigation::Menu(renderer) {}

UiPage::~UiPage() { discardOverlayFrame(); }

void UiPage::onEnter() {
  Activity::onEnter();
  updateRequired = true;
}

void UiPage::loop() {
  if (menuInput()) return;
  renderPage();
}

bool UiPage::menuInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (isOpen()) {
      close();
      restoreOverlayFrameOnClose();
      updateRequired = true;
      return true;
    }
    return back();
  }

  if (mappedInput.wasTouchSwipeUpForRenderer(renderer) || mappedInput.wasTouchSwipeDownForRenderer(renderer) ||
      mappedInput.wasTouchSwipeLeftForRenderer(renderer) || mappedInput.wasTouchSwipeRightForRenderer(renderer)) {
    const navigation::Menu::Action action = navigation::Menu::handleInput(mappedInput);
    if (action == navigation::Menu::Action::None) return false;
    routeMenuAction(action);
    return true;
  }

  if (mappedInput.hasTouch()) {

    float tapX = 0.0f;
    float tapY = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) {
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
  }

  // A board can expose a touch controller even when the current input frame
  // contains no touch event. Do not let that capability check swallow the
  // physical navigation buttons used by the hidden menu.
  const navigation::Menu::Action action = navigation::Menu::handleInput(mappedInput);
  if (action == navigation::Menu::Action::None) return false;
  routeMenuAction(action);
  return true;
}

void UiPage::renderPage() {
#ifndef SIMULATOR
  const unsigned long now = millis();
  if (lastPowerPollMs_ == 0 || now - lastPowerPollMs_ >= 1500) {
    lastPowerPollMs_ = now;
    const bool connected = gpio.isUsbConnected();
    if (powerStateKnown_ && connected != lastExternalPower_) {
      discardOverlayFrame();
      updateRequired = true;
    }
    lastExternalPower_ = connected;
    powerStateKnown_ = true;
  }
#endif
  if (!updateRequired) return;
  render();
  updateRequired = false;
}

bool UiPage::storeOverlayFrame() {
  renderer.syncWriteBufferFromActive();
  renderer.freeBwBufferChunks();
  overlayFrameStored_ = renderer.storeBwBuffer();
  overlayFrameActive_ = overlayFrameStored_;
  overlayFrameRestorePending_ = false;
  return overlayFrameStored_;
}

void UiPage::restoreOverlayFrameOnClose() {
  if (!overlayFrameStored_) return;
  overlayFrameActive_ = false;
  overlayFrameRestorePending_ = true;
}

void UiPage::discardOverlayFrame() {
  if (!overlayFrameStored_) return;
  renderer.freeBwBufferChunks();
  overlayFrameStored_ = false;
  overlayFrameActive_ = false;
  overlayFrameRestorePending_ = false;
}

bool UiPage::routeMenuAction(const navigation::Menu::Action action) {
  switch (action) {
    case navigation::Menu::Action::Opened:
      // Capture the underlying page only on the initial open. Selection
      // changes while the drawer is already open also return Opened.
      if (!overlayFrameStored_) storeOverlayFrame();
      updateRequired = true;
      break;
    case navigation::Menu::Action::Closed:
      restoreOverlayFrameOnClose();
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
    case navigation::Menu::Action::Apps:
      onGoToApps();
      return true;
    case navigation::Menu::Action::None:
    default:
      break;
  }
  return false;
}

void UiPage::menu() { navigation::Menu::render(); }

void UiPage::refresh() { LibraryIndexRefresh::start(renderer, this); }

void UiPage::search() { openSearchFromCallback([] { onGoToHome(); }); }

bool UiPage::back() {
  onGoToHome();
  return true;
}

bool UiPage::menuAction(const navigation::Menu::Action action) {
  (void)action;
  return false;
}

void UiPage::render() {
  renderer.setRenderMode(GfxRenderer::BW);
  bool restored = false;
  if (overlayFrameStored_ && (overlayFrameActive_ || overlayFrameRestorePending_)) {
    restored = renderer.copyStoredBwToFramebuffer();
    if (!restored) {
      discardOverlayFrame();
    } else if (overlayFrameRestorePending_) {
      renderer.freeBwBufferChunks();
      overlayFrameStored_ = false;
      overlayFrameRestorePending_ = false;
    }
  }

  if (!restored) {
    renderer.clearScreen();
    content();
  }

  menu();
  renderer.displayBufferAsync();
}

int UiPage::contentBottomY(const GfxRenderer& renderer) {
  return renderer.getScreenHeight() - navigation::Menu::bottomInset() - CONTENT_BOTTOM_PADDING;
}

int UiPage::tabBarTopY(const GfxRenderer& renderer) {
  return renderer.getScreenHeight() - navigation::Menu::bottomInset();
}

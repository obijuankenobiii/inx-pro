/**
 * @file SyncActivity.cpp
 * @brief Definitions for SyncActivity.
 */

#include "SyncActivity.h"

#include <BoardConfig.h>

#include "system/UiLayout.h"

#include <GfxRenderer.h>

#include "activity/settings/DeviceInfoActivity.h"
#include "activity/settings/DictionaryPickerActivity.h"
#include "activity/settings/KOReaderSettingsActivity.h"
#include "activity/settings/OtaUpdateActivity.h"
#if FREEINK_CAP_USB_MSC
#include "activity/network/UsbFileTransferActivity.h"
#endif
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

extern void onGoToHome();

namespace {
constexpr int kCalibreIndex = 1;
#if FREEINK_CAP_USB_MSC
constexpr int kUsbFileTransferIndex = 2;
constexpr int kCreateHotspotIndex = 3;
constexpr int kOpdsBrowserIndex = 4;
constexpr int kKoReaderSyncIndex = 5;
constexpr int kCheckForUpdatesIndex = 6;
constexpr int kChooseDictionaryIndex = 7;
constexpr int kDeviceInformationIndex = 8;
constexpr int menuItemCount = 9;
const char* menuItems[menuItemCount] = {"Manage via wifi",    "Calibre File Transfer", "USB File Transfer",
                                        "Create hotspot",    "OPDS Browser",          "KOReader Sync",
                                        "Check for updates", "Choose dictionary",      "Device Information"};
#else
constexpr int kCreateHotspotIndex = 2;
constexpr int kOpdsBrowserIndex = 3;
constexpr int kKoReaderSyncIndex = 4;
constexpr int kCheckForUpdatesIndex = 5;
constexpr int kChooseDictionaryIndex = 6;
constexpr int kDeviceInformationIndex = 7;
constexpr int menuItemCount = 8;
const char* menuItems[menuItemCount] = {"Manage via wifi",   "Calibre File Transfer", "Create hotspot",
                                        "OPDS Browser",      "KOReader Sync",         "Check for updates",
                                        "Choose dictionary", "Device Information"};
#endif
constexpr int listItemHeight = UiLayout::LIST_ITEM_HEIGHT;
constexpr int headerTop = 20;
constexpr int headerHeight = 40;
constexpr int listGap = 30;
}

void SyncActivity::onEnter() {
  UiPage::onEnter();
  SETTINGS.runHalfRefreshOnLoadIfEnabled(renderer, SystemSetting::RefreshOnLoadPage::Sync);
}

void SyncActivity::activateItem(const int index) {
  if (index < 0 || index >= menuItemCount) return;

  NetworkMode mode = NetworkMode::JOIN_NETWORK;
  if (index == kCalibreIndex) mode = NetworkMode::CONNECT_CALIBRE;
  if (index == kCreateHotspotIndex) mode = NetworkMode::CREATE_HOTSPOT;
  if (index == kOpdsBrowserIndex) mode = NetworkMode::OPDS_BROWSER;

#if FREEINK_CAP_USB_MSC
  if (index == kUsbFileTransferIndex) {
    enter(new UsbFileTransferActivity(renderer, mappedInput, [this] {
      exit();
      updateRequired = true;
    }));
    return;
  }
#endif

  if (index == kKoReaderSyncIndex) {
    enter(new KOReaderSettingsActivity(renderer, mappedInput, [this] {
      exit();
      updateRequired = true;
    }));
    return;
  }
  if (index == kCheckForUpdatesIndex) {
    enter(new OtaUpdateActivity(renderer, mappedInput, [this] {
      exit();
      updateRequired = true;
    }));
    return;
  }
  if (index == kChooseDictionaryIndex) {
    enter(new DictionaryPickerActivity(renderer, mappedInput, [this] {
      exit();
      updateRequired = true;
    }));
    return;
  }
  if (index == kDeviceInformationIndex) {
    enter(new DeviceInfoActivity(renderer, mappedInput, [this] {
      exit();
      updateRequired = true;
    }));
    return;
  }

  if (onModeSelected) onModeSelected(mode);
}

void SyncActivity::loop() {
  // Let the shared page shell handle the hidden menu first.  The hidden menu
  // opens from the bottom edge on an upward swipe; handling the page's generic
  // upward-swipe guard first would consume that gesture on Device Management.
  if (!subActivity && menuInput()) return;

  if (!subActivity && mappedInput.hasTouch() && mappedInput.wasTouchSwipeUpForRenderer(renderer)) {
    return;
  }

  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (isOpen()) {
    renderPage();
    return;
  }

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
      const int listStartY = headerTop + headerHeight + listGap;
      const int contentBottom = renderer.getScreenHeight() - navigation::Menu::bottomInset() - 10;
      const int tappedIndex = (tapY - listStartY) / listItemHeight;

      if (tapY >= listStartY && tapY < contentBottom && tappedIndex >= 0 && tappedIndex < menuItemCount) {
        INX_SERIAL.printf("[DEVICE][TOUCH] row=%d label=%s\n", tappedIndex, menuItems[tappedIndex]);
        activateItem(tappedIndex);
        return;
      } else if (routeMenuAction(navigation::Menu::handleTap(tapX, tapY))) {
        return;
      }
    }
  }

  renderPage();
}

void SyncActivity::content() {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  const int listStartY = headerTop + headerHeight + listGap;

  const int contentBottom = screenHeight - navigation::Menu::bottomInset() - 10;
  for (int index = 0; index < menuItemCount; ++index) {
    const int itemY = listStartY + index * listItemHeight;
    if (itemY >= contentBottom || itemY + listItemHeight <= listStartY) continue;

    const int titleY = itemY +
                       (listItemHeight - renderer.text.getLineHeight(systemFontId())) / 2;
    renderer.text.render(systemFontId(), 20, titleY, menuItems[index], true);
    const int caretWidth = renderer.text.getWidth(systemFontId(), "›");
    renderer.text.render(systemFontId(), screenWidth - caretWidth - 30, titleY, "›", true);

    if (index + 1 < menuItemCount) {
      renderer.line.render(0, itemY + listItemHeight - 1, screenWidth, itemY + listItemHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }
}

void SyncActivity::onExit() {
  exit();
  UiPage::onExit();
}

void SyncActivity::enter(Activity* activity) {
  if (!activity) return;
  subActivity.reset(activity);
  subActivity->onEnter();
}

void SyncActivity::exit() {
  if (!subActivity) return;
  subActivity->onExit();
  subActivity.reset();
}

/**
 * @file SyncActivity.cpp
 * @brief Definitions for SyncActivity.
 */

#include "SyncActivity.h"

#include <algorithm>

#include <BoardConfig.h>

#include <GfxRenderer.h>

#include "activity/settings/DeviceInfoActivity.h"
#include "activity/settings/KOReaderSettingsActivity.h"
#include "activity/settings/OtaUpdateActivity.h"
#if FREEINK_CAP_USB_MSC
#include "activity/network/UsbFileTransferActivity.h"
#endif
#include "images/DeviceManagementIcons.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

extern void onGoToHome();

namespace {
struct DeviceItem {
  const char* label;
  const char* description;
  const uint8_t* icon;
};

constexpr int kCalibreIndex = 1;
constexpr int kIconSize = 40;
constexpr int kSideMargin = 20;
constexpr int kIconX = 24;
constexpr int kTextX = 88;
constexpr int kSubtitleGap = 5;
constexpr int kRowHeight = 100;
constexpr int kCompactRowHeight = 94;

int rowHeight() { return SETTINGS.hiddenMenu == 0 ? kCompactRowHeight : kRowHeight; }
#if FREEINK_CAP_USB_MSC
constexpr int kUsbFileTransferIndex = 2;
constexpr int kCreateHotspotIndex = 3;
constexpr int kKoReaderSyncIndex = 4;
constexpr int kCheckForUpdatesIndex = 5;
constexpr int kDeviceInformationIndex = 6;
constexpr int menuItemCount = 7;
constexpr DeviceItem menuItems[menuItemCount] = {
    {"Manage via wifi", "Manage books, settings, and files from your browser", DeviceManagementManageWifi},
    {"Calibre File Transfer", "Send books to the device from Calibre", DeviceManagementCalibreTransfer},
    {"USB File Transfer", "Transfer books directly over USB", DeviceManagementUsbTransfer},
    {"Create hotspot", "Create a hotspot for wireless device management", DeviceManagementHotspot},
    {"KOReader Sync", "Synchronize reading progress with KOReader", DeviceManagementKoreaderSync},
    {"Check for updates", "Check for firmware updates", DeviceManagementUpdates},
    {"Device Information", "View device and firmware information", DeviceManagementDeviceInfo},
};
#else
constexpr int kCreateHotspotIndex = 2;
constexpr int kKoReaderSyncIndex = 3;
constexpr int kCheckForUpdatesIndex = 4;
constexpr int kDeviceInformationIndex = 5;
constexpr int menuItemCount = 6;
constexpr DeviceItem menuItems[menuItemCount] = {
    {"Manage via wifi", "Manage books, settings, and files from your browser", DeviceManagementManageWifi},
    {"Calibre File Transfer", "Send books to the device from Calibre", DeviceManagementCalibreTransfer},
    {"Create hotspot", "Create a hotspot for wireless device management", DeviceManagementHotspot},
    {"KOReader Sync", "Synchronize reading progress with KOReader", DeviceManagementKoreaderSync},
    {"Check for updates", "Check for firmware updates", DeviceManagementUpdates},
    {"Device Information", "View device and firmware information", DeviceManagementDeviceInfo},
};
#endif
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
  // Let the shared page shell handle the hidden menu first. The hidden menu
  // opens from the left edge on a right swipe.
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
      const int listStartY = FREEINK_DEVICE_X4PRO ? 80 : 70;
      const int contentBottom = renderer.getScreenHeight() - navigation::Menu::bottomInset() - 10;
      const int currentRowHeight = rowHeight();
      const int tappedIndex = (tapY - listStartY) / currentRowHeight;

      if (tapY >= listStartY && tapY < contentBottom && tappedIndex >= 0 && tappedIndex < menuItemCount) {
        INX_SERIAL.printf("[DEVICE][TOUCH] row=%d label=%s\n", tappedIndex, menuItems[tappedIndex].label);
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
  const int listStartY = FREEINK_DEVICE_X4PRO ? 80 : 70;
  const int currentRowHeight = rowHeight();
  const int titleFont = systemFontId();
  const int subtitleFont = systemFontIdForPointSize(8);
  const int maxTextWidth = std::max(1, screenWidth - kTextX - kSideMargin);
  const int titleHeight = renderer.text.getLineHeight(titleFont);
  const int subtitleHeight = renderer.text.getLineHeight(subtitleFont);
  const int contentHeight = titleHeight + kSubtitleGap + subtitleHeight;
  const int contentBottom = screenHeight - navigation::Menu::bottomInset() - 10;

  for (int index = 0; index < menuItemCount; ++index) {
    const int itemY = listStartY + index * currentRowHeight;
    if (itemY >= contentBottom || itemY + currentRowHeight <= listStartY) continue;

    const int titleY = itemY + (currentRowHeight - contentHeight) / 2;
    renderer.bitmap.icon(menuItems[index].icon, kIconX,
                         itemY + (currentRowHeight - kIconSize) / 2, kIconSize,
                         kIconSize);
    renderer.text.render(titleFont, kTextX, titleY, menuItems[index].label, true, EpdFontFamily::BOLD);
    const std::string subtitle = renderer.text.truncate(subtitleFont, menuItems[index].description, maxTextWidth);
    renderer.text.render(subtitleFont, kTextX, titleY + titleHeight + kSubtitleGap, subtitle.c_str(), true,
                         EpdFontFamily::REGULAR);

    if (index + 1 < menuItemCount) {
      renderer.line.render(kSideMargin, itemY + currentRowHeight - 1, screenWidth - kSideMargin,
                           itemY + currentRowHeight - 1, true, LineRender::Style::Dotted);
    }
  }

  mappedInput.mapLabels("\xC2\xAB Back", "Open", "Up", "Down");
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

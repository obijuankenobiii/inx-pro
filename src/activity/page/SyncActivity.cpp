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
#if FREEINK_CAP_BLE_HID_HOST
#include "activity/settings/BluetoothActivity.h"
#endif
#if FREEINK_CAP_USB_MSC
#include "activity/network/UsbFileTransferActivity.h"
#endif
#include "images/DeviceManagementIcons.h"
#include "images/DeviceManagementCustomIcons.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

extern void onGoToHome();

namespace {
struct DeviceItem {
  const char* label;
  const char* description;
  const uint8_t* icon;
  const char* tag;
};

constexpr int kIconSize = 40;
constexpr int kSideMargin = 20;
constexpr int kIconX = 24;
constexpr int kTextX = 88;
constexpr int kSubtitleGap = 5;
constexpr int kRowHeight = 100;
constexpr int kCompactRowHeight = 92;

int rowHeight() { return SETTINGS.hiddenMenu == 0 ? kCompactRowHeight : kRowHeight; }

#if FREEINK_CAP_USB_MSC
constexpr int kCalibreIndex = 1;
constexpr int kUsbFileTransferIndex = 2;
constexpr int kCreateHotspotIndex = 3;
constexpr DeviceItem fileTransferItems[] = {
    {"Wi-fi", "Manage books, settings, and files from your browser", DeviceManagementManageWifi, nullptr},
    {"Calibre", "Send books to the device from Calibre", DeviceManagementCalibreTransfer, nullptr},
    {"USB", "Transfer books directly over USB", DeviceManagementUsbTransfer, nullptr},
    {"Hotspot", "Create a hotspot for wireless device management", DeviceManagementHotspot, nullptr},
};
#else
constexpr int kCalibreIndex = 1;
constexpr int kCreateHotspotIndex = 2;
constexpr DeviceItem fileTransferItems[] = {
    {"Wi-fi", "Manage books, settings, and files from your browser", DeviceManagementManageWifi, nullptr},
    {"Calibre", "Send books to the device from Calibre", DeviceManagementCalibreTransfer, nullptr},
    {"Hotspot", "Create a hotspot for wireless device management", DeviceManagementHotspot, nullptr},
};
#endif

#if FREEINK_CAP_BLE_HID_HOST
constexpr int kBluetoothIndex = 1;
constexpr int kKoReaderSyncIndex = 2;
constexpr int kCheckForUpdatesIndex = 3;
constexpr int kDeviceInformationIndex = 4;
constexpr DeviceItem menuItems[] = {
    {"File Transfer", "Wi-fi, Calibre, USB, and hotspot", DeviceManagementFileTransfer, nullptr},
    {"Bluetooth", "Connect BLE remotes and map buttons", DeviceManagementBluetooth, nullptr},
    {"KOReader Sync", "Synchronize reading progress with KOReader", DeviceManagementKoreaderSync, nullptr},
    {"Check for updates", "Check for firmware updates", DeviceManagementUpdates, nullptr},
    {"Device Information", "View device and firmware information", DeviceManagementDeviceInfo, nullptr},
};
#else
constexpr int kKoReaderSyncIndex = 1;
constexpr int kCheckForUpdatesIndex = 2;
constexpr int kDeviceInformationIndex = 3;
constexpr DeviceItem menuItems[] = {
    {"File Transfer", "Wi-fi, Calibre, USB, and hotspot", DeviceManagementFileTransfer, nullptr},
    {"KOReader Sync", "Synchronize reading progress with KOReader", DeviceManagementKoreaderSync, nullptr},
    {"Check for updates", "Check for firmware updates", DeviceManagementUpdates, nullptr},
    {"Device Information", "View device and firmware information", DeviceManagementDeviceInfo, nullptr},
};
#endif

constexpr int fileTransferItemCount = static_cast<int>(sizeof(fileTransferItems) / sizeof(fileTransferItems[0]));
constexpr int menuItemCount = static_cast<int>(sizeof(menuItems) / sizeof(menuItems[0]));
}

void SyncActivity::onEnter() {
  UiPage::onEnter();
  showFileTransfer_ = false;
  selectedIndex_ = 0;
  SETTINGS.runHalfRefreshOnLoadIfEnabled(renderer, SystemSetting::RefreshOnLoadPage::Sync);
}

void SyncActivity::activateItem(const int index) {
  const int itemCount = showFileTransfer_ ? fileTransferItemCount : menuItemCount;
  if (index < 0 || index >= itemCount) return;

  if (!showFileTransfer_) {
    if (index == 0) {
      showFileTransfer_ = true;
      selectedIndex_ = 0;
      updateRequired = true;
      return;
    }
#if FREEINK_CAP_BLE_HID_HOST
    if (index == kBluetoothIndex) {
      enter(new BluetoothActivity(renderer, mappedInput, [this] {
        exit();
        updateRequired = true;
      }));
      return;
    }
#endif
  }

  NetworkMode mode = NetworkMode::JOIN_NETWORK;
  if (showFileTransfer_ && index == kCalibreIndex) mode = NetworkMode::CONNECT_CALIBRE;
  if (showFileTransfer_ && index == kCreateHotspotIndex) mode = NetworkMode::CREATE_HOTSPOT;

#if FREEINK_CAP_USB_MSC
  if (showFileTransfer_ && index == kUsbFileTransferIndex) {
    enter(new UsbFileTransferActivity(renderer, mappedInput, [this] {
      exit();
      updateRequired = true;
    }));
    return;
  }
#endif

  if (!showFileTransfer_ && index == kKoReaderSyncIndex) {
    enter(new KOReaderSettingsActivity(renderer, mappedInput, [this] {
      exit();
      updateRequired = true;
    }));
    return;
  }
  if (!showFileTransfer_ && index == kCheckForUpdatesIndex) {
    enter(new OtaUpdateActivity(renderer, mappedInput, [this] {
      exit();
      updateRequired = true;
    }));
    return;
  }
  if (!showFileTransfer_ && index == kDeviceInformationIndex) {
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

  const int itemCount = showFileTransfer_ ? fileTransferItemCount : menuItemCount;
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

      if (tapY >= listStartY && tapY < contentBottom && tappedIndex >= 0 && tappedIndex < itemCount) {
        const DeviceItem* items = showFileTransfer_ ? fileTransferItems : menuItems;
        INX_SERIAL.printf("[DEVICE][TOUCH] row=%d label=%s\n", tappedIndex, items[tappedIndex].label);
        selectedIndex_ = tappedIndex;
        activateItem(tappedIndex);
        renderPage();
        return;
      } else if (routeMenuAction(navigation::Menu::handleTap(tapX, tapY))) {
        return;
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedIndex_ = (selectedIndex_ + itemCount - 1) % itemCount;
    updateRequired = true;
    renderPage();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedIndex_ = (selectedIndex_ + 1) % itemCount;
    updateRequired = true;
    renderPage();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activateItem(selectedIndex_);
    renderPage();
    return;
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
  const DeviceItem* items = showFileTransfer_ ? fileTransferItems : menuItems;
  const int itemCount = showFileTransfer_ ? fileTransferItemCount : menuItemCount;

  const int tagFont = systemFontIdForPointSize(8);
  const int tagLineHeight = renderer.text.getLineHeight(tagFont);
  for (int index = 0; index < itemCount; ++index) {
    const int itemY = listStartY + index * currentRowHeight;
    if (itemY >= contentBottom || itemY + currentRowHeight <= listStartY) continue;

    const int titleY = itemY + (currentRowHeight - contentHeight) / 2;
    renderer.bitmap.icon(items[index].icon, kIconX,
                         itemY + (currentRowHeight - kIconSize) / 2, kIconSize,
                         kIconSize);
    renderer.text.render(titleFont, kTextX, titleY, items[index].label, true, EpdFontFamily::REGULAR);
    if (items[index].tag != nullptr) {
      const int labelWidth = renderer.text.getWidth(titleFont, items[index].label);
      renderer.text.renderGray(tagFont, kTextX + labelWidth + 12,
                               titleY + (titleHeight - tagLineHeight) / 2, items[index].tag, true,
                               EpdFontFamily::REGULAR);
    }
    const std::string subtitle = renderer.text.truncate(subtitleFont, items[index].description, maxTextWidth);
    renderer.text.render(subtitleFont, kTextX, titleY + titleHeight + kSubtitleGap, subtitle.c_str(), true,
                         EpdFontFamily::REGULAR);

    if (index + 1 < itemCount) {
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

bool SyncActivity::back() {
  if (showFileTransfer_) {
    showFileTransfer_ = false;
    selectedIndex_ = 0;
    updateRequired = true;
    return true;
  }
  return UiPage::back();
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

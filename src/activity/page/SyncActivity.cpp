/**
 * @file SyncActivity.cpp
 * @brief Definitions for SyncActivity.
 */

#include "SyncActivity.h"

#include "system/UiLayout.h"

#include <GfxRenderer.h>

#include "activity/network/BackupRestoreActivity.h"
#include "activity/settings/DeviceInfoActivity.h"
#include "activity/settings/DictionaryPickerActivity.h"
#include "activity/settings/KOReaderSettingsActivity.h"
#include "activity/settings/OtaUpdateActivity.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

extern void onGoToHome();

namespace {
constexpr int menuItemCount = 9;
const char* menuItems[menuItemCount] = {"Manage via wifi",   "Connect to calibre", "Create hotspot",
                                        "OPDS Browser",      "Backup and restore", "KOReader Sync",
                                        "Check for updates", "Choose dictionary",  "Device Information"};
constexpr int listItemHeight = UiLayout::LIST_ITEM_HEIGHT;
constexpr int headerTop = 20;
constexpr int headerHeight = 40;
constexpr int listGap = 30;
}  // namespace

void SyncActivity::onEnter() {
  Page::onEnter();
  selectedIndex = 0;
  selectedVisible = false;
  SETTINGS.runHalfRefreshOnLoadIfEnabled(renderer, SystemSetting::RefreshOnLoadPage::Sync);
}

void SyncActivity::loop() {
  if (!subActivity && menuInput()) return;

  if (subActivity) {
    subActivity->loop();
    return;
  }

  // A visible popup owns all input until it is closed or an item is chosen.
  if (isOpen()) {
    renderPage();
    return;
  }

  bool confirmPressed = mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool upPressed = mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool downPressed = mappedInput.wasPressed(MappedInputManager::Button::Down);

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
      const int listStartY = headerTop + headerHeight + listGap;
      const int contentBottom = renderer.getScreenHeight() - navigation::Menu::bottomHeight - 10;
      const int tappedIndex = (tapY - listStartY) / listItemHeight;

      if (tapY >= listStartY && tapY < contentBottom && tappedIndex >= 0 && tappedIndex < menuItemCount) {
        selectedIndex = tappedIndex;
        selectedVisible = true;
        confirmPressed = true;
        INX_SERIAL.printf("[STICKY][TOUCH] DEVICE_ROW index=%d label=%s\n", selectedIndex, menuItems[selectedIndex]);
      } else if (routeMenuAction(navigation::Menu::handleTap(tapX, tapY))) {
        return;
      }
    }
  }

  if (confirmPressed) {
    NetworkMode mode = NetworkMode::JOIN_NETWORK;
    if (selectedIndex == 1) mode = NetworkMode::CONNECT_CALIBRE;
    if (selectedIndex == 2) mode = NetworkMode::CREATE_HOTSPOT;
    if (selectedIndex == 3) mode = NetworkMode::OPDS_BROWSER;

    if (selectedIndex == 4) {
      enter(new BackupRestoreActivity(renderer, mappedInput, [this] {
        exit();
        selectedVisible = false;
        updateRequired = true;
      }));
      return;
    }
    if (selectedIndex == 5) {
      enter(new KOReaderSettingsActivity(renderer, mappedInput, [this] {
        exit();
        selectedVisible = false;
        updateRequired = true;
      }));
      return;
    }
    if (selectedIndex == 6) {
      enter(new OtaUpdateActivity(renderer, mappedInput, [this] {
        exit();
        selectedVisible = false;
        updateRequired = true;
      }));
      return;
    }
    if (selectedIndex == 7) {
      enter(new DictionaryPickerActivity(renderer, mappedInput, [this] {
        exit();
        selectedVisible = false;
        updateRequired = true;
      }));
      return;
    }
    if (selectedIndex == 8) {
      enter(new DeviceInfoActivity(renderer, mappedInput, [this] {
        exit();
        updateRequired = true;
      }));
      return;
    }

    if (onModeSelected) onModeSelected(mode);
    return;
  }

  if (upPressed) {
    selectedIndex = (selectedIndex + menuItemCount - 1) % menuItemCount;
    selectedVisible = true;
    updateRequired = true;
  }
  if (downPressed) {
    selectedIndex = (selectedIndex + 1) % menuItemCount;
    selectedVisible = true;
    updateRequired = true;
  }

  renderPage();
}

void SyncActivity::content() {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  const int listStartY = headerTop + headerHeight + listGap;

  const int contentBottom = screenHeight - navigation::Menu::bottomHeight - 10;
  for (int index = 0; index < menuItemCount; ++index) {
    const int itemY = listStartY + index * listItemHeight;
    if (itemY >= contentBottom || itemY + listItemHeight <= listStartY) continue;

    const bool selected = selectedVisible && index == selectedIndex;
    if (selected) {
      renderer.rectangle.fill(0, itemY, screenWidth, listItemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    const int titleY = itemY +
                       (listItemHeight - renderer.text.getLineHeight(systemFontId())) / 2;
    renderer.text.render(systemFontId(), 20, titleY, menuItems[index], !selected);
    const int caretWidth = renderer.text.getWidth(systemFontId(), "›");
    renderer.text.render(systemFontId(), screenWidth - caretWidth - 30, titleY, "›", !selected);

    if (index + 1 < menuItemCount) {
      renderer.line.render(0, itemY + listItemHeight - 1, screenWidth, itemY + listItemHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }
}

void SyncActivity::onExit() {
  exit();
  Page::onExit();
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

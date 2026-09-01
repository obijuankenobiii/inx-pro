/**
 * @file Menu.cpp
 * @brief Menu rendering for the redesigned Page shell.
 */

#include "Menu.h"
#include "activity/page/Page.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include <climits>
#include <cstdlib>
#include <cstring>

#include "images/Library.h"
#include "images/Recent.h"
#include "images/Search.h"
#include "images/Setting.h"
#include "images/Stats.h"
#include "images/Sync.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"

namespace {
constexpr int itemCount = 5;
constexpr int shownCount = 4;
constexpr int itemSize = 72;
constexpr int cellSize = 120;
constexpr int itemGap = 24;
constexpr int popupSize = 320;
constexpr int popupPadding = 28;

struct Item {
  const uint8_t* icon;
  navigation::Menu::Action action;
};

const Item items[itemCount] = {
    {Recent, navigation::Menu::Action::Home},
    {Library, navigation::Menu::Action::Library},
    {Setting, navigation::Menu::Action::Settings},
    {Sync, navigation::Menu::Action::Sync},
    {Stats, navigation::Menu::Action::Stats},
};

int hidden(const navigation::Menu& menu) {
  const char* page = menu.name();
  if (page && (std::strcmp(page, "Library") == 0)) return 1;
  if (page && (std::strcmp(page, "Settings") == 0)) return 2;
  if (page && (std::strcmp(page, "Stats") == 0 || std::strcmp(page, "Statistics") == 0 ||
               std::strcmp(page, "Reading Stats") == 0))
    return 4;
  if (page && (std::strcmp(page, "Sync") == 0 || std::strcmp(page, "Device Management") == 0 ||
               std::strcmp(page, "Network Settings") == 0))
    return 3;
  return 0;
}

int actual(const navigation::Menu& menu, const int shown) {
  const int skip = hidden(menu);
  int count = 0;
  for (int index = 0; index < itemCount; ++index) {
    if (index == skip) continue;
    if (count++ == shown) return index;
  }
  return -1;
}

int width() { return popupSize; }

int heightFor(const GfxRenderer& renderer) {
  (void)renderer;
  return popupSize;
}

int x(const GfxRenderer& renderer) {
  return (renderer.getScreenWidth() - width()) / 2;
}

int y(const GfxRenderer& renderer) {
  return (renderer.getScreenHeight() - heightFor(renderer)) / 2;
}

int cellX(const GfxRenderer& renderer, const int column) {
  return x(renderer) + popupPadding + column * (cellSize + itemGap);
}

int cellY(const GfxRenderer& renderer, const int row) {
  return y(renderer) + popupPadding + row * (cellSize + itemGap);
}

int itemAt(const navigation::Menu& menu, const GfxRenderer& renderer, const int tapX, const int tapY) {
  for (int shown = 0; shown < shownCount; ++shown) {
    const int column = shown % 2;
    const int row = shown / 2;
    const int x = cellX(renderer, column);
    const int y = cellY(renderer, row);
    if (tapX >= x && tapX < x + cellSize && tapY >= y && tapY < y + cellSize) {
      return shown;
    }
  }
  return -1;
}

bool current(const navigation::Menu& menu, const navigation::Menu::Action action) {
  const char* page = menu.name();
  if (!page) return false;
  switch (action) {
    case navigation::Menu::Action::Home:
      return std::strcmp(page, "Home") == 0;
    case navigation::Menu::Action::Library:
      return std::strcmp(page, "Library") == 0;
    case navigation::Menu::Action::Settings:
      return std::strcmp(page, "Settings") == 0;
    case navigation::Menu::Action::Stats:
      return std::strcmp(page, "Stats") == 0 || std::strcmp(page, "Statistics") == 0 ||
             std::strcmp(page, "Reading Stats") == 0;
    case navigation::Menu::Action::Sync:
      return std::strcmp(page, "Sync") == 0 || std::strcmp(page, "Device Management") == 0 ||
             std::strcmp(page, "Network Settings") == 0;
    default:
      return false;
  }
}

}

namespace navigation {

Menu::Menu(GfxRenderer& renderer)
    : menuRenderer(renderer)
#if FREEINK_DEVICE_X4PRO
      , lightDrawer(renderer)
#endif
{}

MappedInputManager::Button Menu::tabPrevButton() const { return MappedInputManager::Button::Left; }

MappedInputManager::Button Menu::tabNextButton() const { return MappedInputManager::Button::Right; }

MappedInputManager::Button Menu::itemPrevButton() const { return MappedInputManager::Button::Up; }

MappedInputManager::Button Menu::itemNextButton() const { return MappedInputManager::Button::Down; }

void Menu::drawBattery(const GfxRenderer& renderer) const {
  constexpr int batteryRightMargin = 10;
  const bool showPercentage = SETTINGS.hideBatteryPercentage != SystemSetting::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int textWidth = showPercentage ? renderer.text.getWidth(MONTSERRAT_8_FONT_ID, "100%") : 0;
  const int width = showPercentage ? ScreenComponents::BATTERY_ICON_WIDTH + ScreenComponents::BATTERY_TEXT_GAP + textWidth
                                   : ScreenComponents::BATTERY_ICON_WIDTH;
  const int x = renderer.getScreenWidth() - batteryRightMargin - width;
  const int y = topPadding + (iconSize - 18) / 2;
  ScreenComponents::drawBattery(renderer, x, y, showPercentage);
}

void Menu::handleTabNavigation(const bool leftPressed, const bool rightPressed) {
  if (leftPressed) {
    tabSelectorIndex = (tabSelectorIndex - 1 + tabCount) % tabCount;
    navigateToSelectedMenu();
  }
  if (rightPressed) {
    tabSelectorIndex = (tabSelectorIndex + 1) % tabCount;
    navigateToSelectedMenu();
  }
}

bool Menu::handleTabBarTouch(MappedInputManager& mappedInput, const GfxRenderer& renderer) {
  if (!mappedInput.hasTouch()) return false;

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) return false;

  const int screenW = renderer.getScreenWidth();
  const int tapX = static_cast<int>(tapNx * screenW);
  const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
  const int tabY = Page::tabBarTopY(renderer);
  const int tabH = 60;
  const int tabButtonWidth = (screenW / tabCount) - 1;

  int tappedTab = 0;
  int bestDist = INT_MAX;
  for (int i = 0; i < tabCount; ++i) {
    const int centerX = i * tabButtonWidth + tabButtonWidth / 2;
    const int dist = std::abs(tapX - centerX);
    if (dist < bestDist) {
      bestDist = dist;
      tappedTab = i;
    }
  }

  constexpr int topTolerance = 60;
  if (tapY < tabY - topTolerance || tapY >= tabY + tabH || tappedTab == tabSelectorIndex) return false;

  tabSelectorIndex = tappedTab;
  navigateToSelectedMenu();
  return true;
}

void Menu::render() const {
  title();
  center();
  navigation();
  bottom();
  popup();
#if FREEINK_DEVICE_X4PRO
  lightDrawer.render();
#endif
}

void Menu::title() const {
  const int font = MONTSERRAT_16_FONT_ID;
  const int textY = topPadding + (iconSize - menuRenderer.text.getLineHeight(font)) / 2;
  const char* title = name();
  if (title && title[0] != '\0') {
    menuRenderer.text.render(font, leftMargin, textY, title, true, EpdFontFamily::BOLD);
  }
}

void Menu::navigation() const {
  constexpr int count = 5;
  const int left = leftMargin + iconSize / 2;
  const int right = menuRenderer.getScreenWidth() - leftMargin - iconSize / 2;
  const int step = count > 1 ? (right - left) / (count - 1) : 0;
  const int centerY = menuRenderer.getScreenHeight() - bottomPadding - bottomSize / 2;
  const int iconY = centerY - iconSize / 2;
  int slot = 0;
  const auto icon = [&](const uint8_t* image, const Action action) {
    const int centerX = left + slot++ * step;
    menuRenderer.bitmap.icon(image, centerX - iconSize / 2, iconY, iconSize, iconSize);
    if (current(*this, action)) {
      constexpr int underline = 36;
      const int underlineY = iconY + iconSize + 5;
      menuRenderer.rectangle.fill(centerX - underline / 2, underlineY, underline, 4, true);
    }
  };
  icon(Recent, Action::Home);
  icon(Library, Action::Library);
  icon(Setting, Action::Settings);
  icon(Sync, Action::Sync);

  const int centerX = left + slot++ * step;
  menuRenderer.circle.render(centerX, centerY + 5, bottomSize / 2, true);
  menuRenderer.bitmap.icon(Search, centerX - iconSize / 2, iconY + 5, iconSize, iconSize,
                           BitmapRender::Orientation::None, true);
}

void Menu::bottom() const {}

void Menu::popup() const {
  if (!open) {
    return;
  }

  const int left = x(menuRenderer);
  const int top = y(menuRenderer);
  const int popupW = width();
  const int popupH = heightFor(menuRenderer);
  menuRenderer.rectangle.fill(left, top, popupW, popupH, false);

  for (int shown = 0; shown < shownCount; ++shown) {
    const int index = actual(*this, shown);
    const int column = shown % 2;
    const int row = shown / 2;
    const int itemX = cellX(menuRenderer, column);
    const int itemY = cellY(menuRenderer, row) + 10;
    const bool active = selectedVisible && shown == selected;

    if (active) {
      menuRenderer.rectangle.fill(itemX, itemY, cellSize, cellSize, true);
    }
    const int iconX = itemX + (cellSize - itemSize) / 2;
    const int iconY = itemY + (cellSize - itemSize) / 2;
    menuRenderer.bitmap.icon(items[index].icon, iconX, iconY, itemSize, itemSize,
                             BitmapRender::Orientation::None, active);
  }
  menuRenderer.rectangle.render(left, top, popupW, popupH, true);
}

Menu::Action Menu::choose() const {
  open = false;
  const int index = actual(*this, selected);
  return index >= 0 ? items[index].action : Action::None;
}

Menu::Action Menu::handleInput(MappedInputManager& input) const {
#if FREEINK_DEVICE_X4PRO
  const LightDrawer::Action lightAction = lightDrawer.handleInput(input);
  if (lightAction == LightDrawer::Action::Opened) {
    open = false;
    return Action::Opened;
  }
  if (lightAction == LightDrawer::Action::Closed) return Action::Closed;
  if (lightAction == LightDrawer::Action::Adjusted) return Action::Opened;
#endif
  const bool touchSwipe = input.wasTouchSwipeUpForRenderer(menuRenderer) ||
                          input.wasTouchSwipeDownForRenderer(menuRenderer) ||
                          input.wasTouchSwipeLeftForRenderer(menuRenderer) ||
                          input.wasTouchSwipeRightForRenderer(menuRenderer);
  if (touchSwipe) {
    ignoreOpeningTap = false;
    return Action::None;
  }

  if (open && input.wasPressed(MappedInputManager::Button::Up)) {
    selected = (selected + shownCount - 1) % shownCount;
    selectedVisible = true;
    return Action::Opened;
  }
  if (open && input.wasPressed(MappedInputManager::Button::Down)) {
    selected = (selected + 1) % shownCount;
    selectedVisible = true;
    return Action::Opened;
  }

  if (!input.hasTouch()) {
    return Action::None;
  }

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!input.wasTouchTapInScreen(menuRenderer, tapNx, tapNy)) {
    return Action::None;
  }

  const int tapX = static_cast<int>(tapNx * menuRenderer.getScreenWidth());
  const int tapY = static_cast<int>(tapNy * menuRenderer.getScreenHeight());

  return handleTap(tapX, tapY);
}

Menu::Action Menu::handleTap(const int tapX, const int tapY) const {
#if FREEINK_DEVICE_X4PRO
  if (lightDrawer.isOpen()) {
    switch (lightDrawer.handleTap(tapX, tapY)) {
      case LightDrawer::Action::Opened:
        return Action::Opened;
      case LightDrawer::Action::Closed:
        return Action::Closed;
      case LightDrawer::Action::None:
        break;
    }
  }
#endif

  if (open) {
    const bool duplicateOpeningTap = ignoreOpeningTap && openingX >= 0 && openingY >= 0 &&
                                     std::abs(tapX - openingX) <= 18 && std::abs(tapY - openingY) <= 18;
    if (duplicateOpeningTap) {
      ignoreOpeningTap = false;
      return Action::None;
    }
    ignoreOpeningTap = false;

    const int menuX = x(menuRenderer);
    const int menuY = y(menuRenderer);
    const int menuW = width();
    const int menuH = heightFor(menuRenderer);
    const bool inside = tapX >= menuX && tapX < menuX + menuW && tapY >= menuY && tapY < menuY + menuH;
    if (!inside) {
      open = false;
      return Action::Closed;
    }

    const int item = itemAt(*this, menuRenderer, tapX, tapY);
    if (item < 0 || item >= shownCount) {
      open = false;
      return Action::Closed;
    }

    open = false;
    selected = item;
    return choose();
  }

  const Action centerAction = centerTap(tapX, tapY);
  if (centerAction != Action::None) {
    return centerAction;
  }

  constexpr int count = 5;
  const int left = leftMargin + iconSize / 2;
  const int right = menuRenderer.getScreenWidth() - leftMargin - iconSize / 2;
  const int step = count > 1 ? (right - left) / (count - 1) : 0;
  const int navigationY = menuRenderer.getScreenHeight() - bottomHeight - 10;
  if (tapY < navigationY || tapY >= menuRenderer.getScreenHeight() || step <= 0) return Action::None;

  int slot = (tapX - left + step / 2) / step;
  if (slot < 0) slot = 0;
  if (slot >= count) slot = count - 1;
  switch (slot) {
    case 0:
      return Action::Home;
    case 1:
      return Action::Library;
    case 2:
      return Action::Settings;
    case 3:
      return Action::Sync;
    case 4:
      return Action::Search;
    default:
      break;
  }

  return Action::None;
}

}

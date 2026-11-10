/**
 * @file Menu.cpp
 * @brief Menu rendering for the redesigned UiPage shell.
 */

#include "Menu.h"
#include "activity/page/Page.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>

#include "images/Library.h"
#include "images/Apps.h"
#include "images/Recent.h"
#include "images/Search.h"
#include "images/Setting.h"
#include "images/StatisticsIcon.h"
#include "images/Sync.h"
#include "activity/page/components/global/Sidebar.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"

namespace {
constexpr int regularItemCount = 6;
constexpr int hiddenItemCount = 7;
constexpr int itemSize = 40;
constexpr int gridColumns = 3;
constexpr int cellSize = 80;
constexpr int itemGap = 16;
constexpr int popupPadding = 24;
constexpr int popupWidth = gridColumns * cellSize + (gridColumns - 1) * itemGap + 2 * popupPadding;
constexpr int popupRows = (regularItemCount + gridColumns - 1) / gridColumns;
constexpr int popupHeight = popupRows * cellSize + (popupRows - 1) * itemGap + 2 * popupPadding;
constexpr int hiddenSidebarTopPadding = 24;
constexpr int hiddenSidebarRowGap = 8;
constexpr int hiddenSidebarIconSourceSize = 40;
constexpr int hiddenSidebarIconSize = 32;
constexpr int hiddenSidebarIconLabelGap = 16;
// Allow for the small edge offset reported by some digitizers while still
// requiring the gesture to originate at the left side of the screen.
constexpr float hiddenSidebarSwipeStartNx = 0.12f;

struct Item {
  const uint8_t* icon;
  navigation::Menu::Action action;
  const char* label;
};

const Item regularItems[regularItemCount] = {
    {Recent, navigation::Menu::Action::Home, "Home"},
    {Library, navigation::Menu::Action::Library, "Library"},
    {Setting, navigation::Menu::Action::Settings, "Settings"},
    {Sync, navigation::Menu::Action::Sync, "Device"},
    {Apps, navigation::Menu::Action::Apps, "Apps"},
    {Search, navigation::Menu::Action::Search, "Search"},
};

// The hidden sidebar has room for Statistics without changing the six-slot
// bottom navigation. Keep Search last in this list so it remains the final
// action in the sidebar.
const Item hiddenItems[hiddenItemCount] = {
    {Recent, navigation::Menu::Action::Home, "Home"},
    {Library, navigation::Menu::Action::Library, "Library"},
    {Setting, navigation::Menu::Action::Settings, "Settings"},
    {Sync, navigation::Menu::Action::Sync, "Device"},
    {Apps, navigation::Menu::Action::Apps, "Apps"},
    {StatisticsIcon, navigation::Menu::Action::Stats, "Statistics"},
    {Search, navigation::Menu::Action::Search, "Search"},
};

const Item& itemForIndex(const int index) {
  return SETTINGS.hiddenMenu != 0 ? hiddenItems[index] : regularItems[index];
}

bool current(const navigation::Menu& menu, navigation::Menu::Action action);

bool excludedFromHiddenDrawer(const navigation::Menu& menu, const int index) {
  // Keep Apps out of the drawer on sub-pages without a matching main-page action;
  // otherwise keep every main-page item visible, including the current page.
  if (index == 4) {
    for (int candidate = 0; candidate < hiddenItemCount; ++candidate) {
      if (current(menu, hiddenItems[candidate].action)) return false;
    }
    return true;
  }
  return false;
}

int visibleCount(const navigation::Menu& menu) {
  if (SETTINGS.hiddenMenu == 0) return regularItemCount;
  int count = 0;
  for (int index = 0; index < hiddenItemCount; ++index) {
    if (!excludedFromHiddenDrawer(menu, index)) ++count;
  }
  return count;
}

int actual(const navigation::Menu& menu, const int shown) {
  if (shown < 0 || shown >= visibleCount(menu)) return -1;
  if (SETTINGS.hiddenMenu == 0) return shown;

  int visible = 0;
  for (int index = 0; index < hiddenItemCount; ++index) {
    if (excludedFromHiddenDrawer(menu, index)) continue;
    if (visible++ == shown) return index;
  }
  return -1;
}

int width(const GfxRenderer& renderer) { return SETTINGS.hiddenMenu != 0 ? Sidebar::width(renderer) : popupWidth; }

int heightFor(const GfxRenderer& renderer) {
  return SETTINGS.hiddenMenu != 0 ? renderer.getScreenHeight() : popupHeight;
}

int x(const GfxRenderer& renderer) { return SETTINGS.hiddenMenu != 0 ? 0 : (renderer.getScreenWidth() - width(renderer)) / 2; }

int y(const GfxRenderer& renderer) { return SETTINGS.hiddenMenu != 0 ? 0 : (renderer.getScreenHeight() - heightFor(renderer)) / 2; }

int cellX(const GfxRenderer& renderer, const int column) {
  return x(renderer) + popupPadding + column * (cellSize + itemGap);
}

int cellY(const GfxRenderer& renderer, const int row) {
  return y(renderer) + popupPadding + row * (cellSize + itemGap);
}

int itemAt(const navigation::Menu& menu, const GfxRenderer& renderer, const int tapX, const int tapY) {
  if (SETTINGS.hiddenMenu != 0) {
    return Sidebar::hitTest(renderer, tapX, tapY, static_cast<size_t>(visibleCount(menu)));
  }

  for (int shown = 0; shown < regularItemCount; ++shown) {
    const int column = shown % gridColumns;
    const int row = shown / gridColumns;
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
      return std::strcmp(page, "Sync") == 0 || std::strcmp(page, "Device") == 0 ||
             std::strcmp(page, "Device Management") == 0 ||
             std::strcmp(page, "Network Settings") == 0;
    case navigation::Menu::Action::Search:
      return std::strcmp(page, "Search") == 0;
    case navigation::Menu::Action::Apps:
      return std::strcmp(page, "Apps") == 0;
    default:
      return false;
  }
}

}

namespace navigation {

Menu::Menu(GfxRenderer& renderer) : menuRenderer(renderer) {}

MappedInputManager::Button Menu::tabPrevButton() const { return MappedInputManager::Button::Left; }

MappedInputManager::Button Menu::tabNextButton() const { return MappedInputManager::Button::Right; }

MappedInputManager::Button Menu::itemPrevButton() const { return MappedInputManager::Button::Up; }

MappedInputManager::Button Menu::itemNextButton() const { return MappedInputManager::Button::Down; }

void Menu::drawBattery(const GfxRenderer& renderer) const {
  const bool showPercentage = SETTINGS.hideBatteryPercentage != SystemSetting::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int textWidth = showPercentage ? renderer.text.getWidth(systemFontIdForPointSize(8), "100%") : 0;
  const int width = showPercentage ? ScreenComponents::BATTERY_ICON_WIDTH + ScreenComponents::BATTERY_TEXT_GAP + textWidth
                                   : ScreenComponents::BATTERY_ICON_WIDTH;
  const int x = renderer.getScreenWidth() - ScreenComponents::BATTERY_RIGHT_MARGIN - width;
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
  const int tabY = UiPage::tabBarTopY(renderer);
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
}

void Menu::title() const {
  const int font = systemFontIdForPointSize(16);
  const int textY = topPadding + (iconSize - menuRenderer.text.getLineHeight(font)) / 2;
  const char* title = name();
  if (title && title[0] != '\0') {
    menuRenderer.text.render(font, leftMargin, textY, title, true, EpdFontFamily::BOLD);
  }
}

void Menu::navigation() const {
  if (SETTINGS.hiddenMenu != 0) return;
  constexpr int count = 6;
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

  const int appsCenterX = left + slot++ * step;
  const bool appsActive = current(*this, Action::Apps);
  menuRenderer.bitmap.icon(Apps, appsCenterX - iconSize / 2, iconY, iconSize, iconSize);
  if (appsActive) {
    constexpr int underline = 36;
    menuRenderer.rectangle.fill(appsCenterX - underline / 2, iconY + iconSize + 5, underline, 4, true);
  }
  const int searchCenterX = left + slot++ * step;
  menuRenderer.circle.render(searchCenterX, centerY + 5, bottomSize / 2, true);
  menuRenderer.bitmap.icon(Search, searchCenterX - iconSize / 2, iconY + 5, iconSize, iconSize,
                           BitmapRender::Orientation::None, true);
}

void Menu::bottom() const {}

void Menu::popup() const {
  if (!open) {
    return;
  }

  const int left = x(menuRenderer);
  const int top = y(menuRenderer);
  const int popupW = width(menuRenderer);
  const int popupH = heightFor(menuRenderer);

  if (SETTINGS.hiddenMenu != 0) {
    Sidebar::renderFrame(menuRenderer, "Menu");
    const int labelFont = systemFontId();
    const int lineHeight = menuRenderer.text.getLineHeight(labelFont);
    for (int shown = 0; shown < visibleCount(*this); ++shown) {
      const int index = actual(*this, shown);
      const int itemY = Sidebar::listTop() + hiddenSidebarTopPadding +
                        shown * (Sidebar::rowHeight + hiddenSidebarRowGap);
      const bool active = selectedVisible && shown == selected;
      const int drawerWidth = Sidebar::width(menuRenderer);
      if (active) {
        menuRenderer.rectangle.fill(0, itemY, drawerWidth, Sidebar::rowHeight, true);
      }
      const int iconX = 24;
      const int iconY = itemY + (Sidebar::rowHeight - hiddenSidebarIconSize) / 2;
      const int labelX = iconX + hiddenSidebarIconSize + hiddenSidebarIconLabelGap;
      const int labelY = itemY + (Sidebar::rowHeight - lineHeight) / 2;
      menuRenderer.bitmap.iconScaled(itemForIndex(index).icon, iconX, iconY, hiddenSidebarIconSourceSize,
                                    hiddenSidebarIconSourceSize, hiddenSidebarIconSize, hiddenSidebarIconSize,
                                    BitmapRender::Orientation::None, active);
      menuRenderer.text.render(labelFont, labelX, labelY, itemForIndex(index).label, !active,
                               EpdFontFamily::REGULAR);
    }
  } else {
    menuRenderer.rectangle.fill(left, top, popupW, popupH, false);
  }

  if (SETTINGS.hiddenMenu != 0) {
    return;
  }

  for (int shown = 0; shown < visibleCount(*this); ++shown) {
    const int index = actual(*this, shown);
    const int column = shown % gridColumns;
    const int row = shown / gridColumns;
    const int itemX = cellX(menuRenderer, column);
    const int itemY = cellY(menuRenderer, row) + 10;
    const bool active = selectedVisible && shown == selected;

    if (active) menuRenderer.rectangle.fill(itemX, itemY, cellSize, cellSize, true);
    const int iconX = itemX + (cellSize - itemSize) / 2;
    const int iconY = itemY + (cellSize - itemSize) / 2;
    menuRenderer.bitmap.icon(itemForIndex(index).icon, iconX, iconY, itemSize, itemSize,
                             BitmapRender::Orientation::None, active);
  }
  menuRenderer.rectangle.render(left, top, popupW, popupH, true);
}

Menu::Action Menu::choose() const {
  open = false;
  const int index = actual(*this, selected);
  return index >= 0 ? itemForIndex(index).action : Action::None;
}

Menu::Action Menu::handleInput(MappedInputManager& input) const {
  if (SETTINGS.hiddenMenu != 0) {
    float startNx = 0.0f;
    float startNy = 0.0f;
    if (open && input.wasTouchSwipeLeftInScreen(menuRenderer, startNx, startNy)) {
      open = false;
      selectedVisible = false;
      return Action::Closed;
    }

    if (!open && input.wasTouchSwipeRightInScreen(menuRenderer, startNx, startNy)) {
      if (startNx <= hiddenSidebarSwipeStartNx) {
        open = true;
        selected = 0;
        selectedVisible = false;
        return Action::Opened;
      }
    }
  }
  const bool touchSwipe = input.wasTouchSwipeUpForRenderer(menuRenderer) ||
                          input.wasTouchSwipeDownForRenderer(menuRenderer) ||
                          input.wasTouchSwipeLeftForRenderer(menuRenderer) ||
                          input.wasTouchSwipeRightForRenderer(menuRenderer);
  if (touchSwipe) {
    return Action::None;
  }

  if (open && input.wasPressed(MappedInputManager::Button::Up)) {
    selected = (selected + visibleCount(*this) - 1) % visibleCount(*this);
    selectedVisible = true;
    return Action::Opened;
  }
  if (open && input.wasPressed(MappedInputManager::Button::Down)) {
    selected = (selected + 1) % visibleCount(*this);
    selectedVisible = true;
    return Action::Opened;
  }
  if (open && input.wasPressed(MappedInputManager::Button::Confirm)) {
    return choose();
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
  if (open) {
    const int menuX = x(menuRenderer);
    const int menuY = y(menuRenderer);
    const int menuW = width(menuRenderer);
    const int menuH = heightFor(menuRenderer);
    const bool inside = tapX >= menuX && tapX < menuX + menuW && tapY >= menuY && tapY < menuY + menuH;
    if (!inside) {
      open = false;
      return Action::Closed;
    }

    const int item = itemAt(*this, menuRenderer, tapX, tapY);
    if (item < 0 || item >= visibleCount(*this)) {
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

  if (SETTINGS.hiddenMenu != 0) return Action::None;

  constexpr int count = 6;
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
      return Action::Apps;
    case 5:
      return Action::Search;
    default:
      break;
  }

  return Action::None;
}

}

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
#include "images/Recent.h"
#include "images/Search.h"
#include "images/Setting.h"
#include "images/Sync.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"

namespace {
constexpr int regularItemCount = 5;
constexpr int hiddenItemCount = 4;
constexpr int itemSize = 40;
constexpr int gridColumns = 3;
constexpr int cellSize = 80;
constexpr int itemGap = 16;
constexpr int popupPadding = 24;
constexpr int popupWidth = gridColumns * cellSize + (gridColumns - 1) * itemGap + 2 * popupPadding;
constexpr int popupRows = (regularItemCount + gridColumns - 1) / gridColumns;
constexpr int popupHeight = popupRows * cellSize + (popupRows - 1) * itemGap + 2 * popupPadding;
constexpr int hiddenDrawerGridColumns = 2;
constexpr int hiddenDrawerGridRows = (hiddenItemCount + hiddenDrawerGridColumns - 1) / hiddenDrawerGridColumns;
constexpr int hiddenDrawerRowHeight = 48;
constexpr int hiddenDrawerRowGap = 40;
constexpr int hiddenDrawerGridTopMargin = 40;
constexpr int hiddenDrawerBottomPadding = 40;
constexpr int hiddenDrawerSideInset = 20;
constexpr int hiddenDrawerColumnGap = 40;
constexpr int hiddenDrawerIconSize = 40;
constexpr int hiddenDrawerIconLabelGap = 20;
constexpr int hiddenDrawerIndicatorWidth = 96;
constexpr int hiddenDrawerIndicatorHeight = 6;
constexpr int hiddenDrawerIndicatorTopMargin = 8;
#if FREEINK_DEVICE_X4PRO
// The X4 Pro digitizer reports the initial contact a little inside the edge.
// Use a larger bottom activation band so the Home page does not consume a
// valid hidden-menu swipe as widget/page navigation.
constexpr float hiddenDrawerSwipeStartNy = 0.75f;
#else
constexpr float hiddenDrawerSwipeStartNy = 0.85f;
#endif

struct Item {
  const uint8_t* icon;
  navigation::Menu::Action action;
  const char* label;
};

const Item items[regularItemCount] = {
    {Recent, navigation::Menu::Action::Home, "Home"},
    {Library, navigation::Menu::Action::Library, "Library"},
    {Setting, navigation::Menu::Action::Settings, "Settings"},
    {Sync, navigation::Menu::Action::Sync, "Manage"},
    {Search, navigation::Menu::Action::Search, "Search"},
};

bool current(const navigation::Menu& menu, navigation::Menu::Action action);

bool excludedFromHiddenDrawer(const navigation::Menu& menu, const int index) {
  if (current(menu, items[index].action)) return true;
  // Keep the drawer at four entries even on pages without a matching main-page action.
  if (index == regularItemCount - 1) {
    for (int candidate = 0; candidate < regularItemCount - 1; ++candidate) {
      if (current(menu, items[candidate].action)) return false;
    }
    return true;
  }
  return false;
}

int visibleCount(const navigation::Menu& menu) {
  if (SETTINGS.hiddenMenu == 0) return regularItemCount;
  int count = 0;
  for (int index = 0; index < regularItemCount; ++index) {
    if (!excludedFromHiddenDrawer(menu, index)) ++count;
  }
  return count;
}

int hiddenDrawerGroupWidth(const GfxRenderer& renderer) {
  const int labelFont = MONTSERRAT_10_FONT_ID;
  int widestLabel = 0;
  for (int index = 0; index < regularItemCount; ++index) {
    widestLabel = std::max(widestLabel, renderer.text.getWidth(labelFont, items[index].label));
  }
  return hiddenDrawerIconSize + hiddenDrawerIconLabelGap + widestLabel;
}

int actual(const navigation::Menu& menu, const int shown) {
  if (shown < 0 || shown >= visibleCount(menu)) return -1;
  if (SETTINGS.hiddenMenu == 0) return shown;

  int visible = 0;
  for (int index = 0; index < regularItemCount; ++index) {
    if (excludedFromHiddenDrawer(menu, index)) continue;
    if (visible++ == shown) return index;
  }
  return -1;
}

int width(const GfxRenderer& renderer) {
  return SETTINGS.hiddenMenu != 0 ? renderer.getScreenWidth() : popupWidth;
}

int heightFor(const GfxRenderer& renderer) {
  (void)renderer;
  return SETTINGS.hiddenMenu != 0
             ? hiddenDrawerGridTopMargin + hiddenDrawerGridRows * hiddenDrawerRowHeight +
                   (hiddenDrawerGridRows - 1) * hiddenDrawerRowGap + hiddenDrawerBottomPadding
             : popupHeight;
}

int x(const GfxRenderer& renderer) {
  return SETTINGS.hiddenMenu != 0 ? 0 : (renderer.getScreenWidth() - width(renderer)) / 2;
}

int y(const GfxRenderer& renderer) {
  return SETTINGS.hiddenMenu != 0 ? renderer.getScreenHeight() - heightFor(renderer)
                                  : (renderer.getScreenHeight() - heightFor(renderer)) / 2;
}

int hiddenIconTop(const GfxRenderer& renderer) {
  return y(renderer) + hiddenDrawerGridTopMargin;
}

int cellX(const GfxRenderer& renderer, const int column) {
  if (SETTINGS.hiddenMenu != 0) {
    const int contentWidth = renderer.getScreenWidth() - hiddenDrawerSideInset * 2;
    const int cellWidth = (contentWidth - hiddenDrawerColumnGap) / hiddenDrawerGridColumns;
    return hiddenDrawerSideInset + column * (cellWidth + hiddenDrawerColumnGap);
  }
  return x(renderer) + popupPadding + column * (cellSize + itemGap);
}

int hiddenCellWidth(const GfxRenderer& renderer) {
  const int contentWidth = renderer.getScreenWidth() - hiddenDrawerSideInset * 2;
  return (contentWidth - hiddenDrawerColumnGap) / hiddenDrawerGridColumns;
}

int cellY(const GfxRenderer& renderer, const int row) {
  if (SETTINGS.hiddenMenu != 0) {
    return hiddenIconTop(renderer) + row * (hiddenDrawerRowHeight + hiddenDrawerRowGap);
  }
  return y(renderer) + popupPadding + row * (cellSize + itemGap);
}

int itemAt(const navigation::Menu& menu, const GfxRenderer& renderer, const int tapX, const int tapY) {
  if (SETTINGS.hiddenMenu != 0) {
    const int menuX = hiddenDrawerSideInset;
    const int contentWidth = renderer.getScreenWidth() - hiddenDrawerSideInset * 2;
    const int slotWidth = hiddenCellWidth(renderer);
    const int top = hiddenIconTop(renderer);
    const int relativeX = tapX - menuX;
    const int relativeY = tapY - top;
    if (tapX < menuX || tapX >= menuX + contentWidth || relativeY < 0 ||
        relativeY >= hiddenDrawerGridRows * hiddenDrawerRowHeight +
                          (hiddenDrawerGridRows - 1) * hiddenDrawerRowGap) {
      return -1;
    }

    // Treat the gap between cells as part of the nearest cell. The drawer is
    // visually laid out with gaps, but the entire grid area should be a
    // reliable touch target rather than requiring a tap on the icon/label.
    const int columnBoundary = slotWidth + hiddenDrawerColumnGap / 2;
    const int rowBoundary = hiddenDrawerRowHeight + hiddenDrawerRowGap / 2;
    const int column = relativeX < columnBoundary ? 0 : 1;
    const int row = relativeY < rowBoundary ? 0 : 1;
    const int shown = row * hiddenDrawerGridColumns + column;
    return shown >= 0 && shown < visibleCount(menu) ? shown : -1;
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
      return std::strcmp(page, "Sync") == 0 || std::strcmp(page, "Device Management") == 0 ||
             std::strcmp(page, "Network Settings") == 0;
    case navigation::Menu::Action::Search:
      return std::strcmp(page, "Search") == 0;
    default:
      return false;
  }
}

}

namespace navigation {

Menu::Menu(GfxRenderer& renderer)
    : menuRenderer(renderer)
#if INX_HAS_FRONTLIGHT || FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO
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

bool Menu::restoreLightDrawerFrameForRender() const {
#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO
  return lightDrawer.restoreStoredFrameForRender();
#else
  return false;
#endif
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
#if INX_HAS_FRONTLIGHT || FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO
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
  if (SETTINGS.hiddenMenu != 0) return;
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
  const int popupW = width(menuRenderer);
  const int popupH = heightFor(menuRenderer);

  if (SETTINGS.hiddenMenu != 0) {
    ScreenComponents::drawWhiteOverlay(menuRenderer, 0);
    menuRenderer.rectangle.fill(left, top, popupW, popupH,
                               static_cast<int>(GfxRenderer::FillTone::Paper));
    menuRenderer.rectangle.render(left, top, popupW, popupH, true);

    const int indicatorX = left + (popupW - hiddenDrawerIndicatorWidth) / 2;
    const int indicatorY = top + hiddenDrawerIndicatorTopMargin;
    const int indicatorRadius = hiddenDrawerIndicatorHeight / 2;
    const int indicatorMiddleWidth = hiddenDrawerIndicatorWidth - 2 * indicatorRadius;
    menuRenderer.rectangle.fill(indicatorX + indicatorRadius, indicatorY, indicatorMiddleWidth,
                                hiddenDrawerIndicatorHeight, static_cast<int>(GfxRenderer::FillTone::Ink), true);
    menuRenderer.circle.render(indicatorX + indicatorRadius, indicatorY + indicatorRadius, indicatorRadius, true);
    menuRenderer.circle.render(indicatorX + hiddenDrawerIndicatorWidth - indicatorRadius - 1,
                               indicatorY + indicatorRadius, indicatorRadius, true);
  } else {
    menuRenderer.rectangle.fill(left, top, popupW, popupH, false);
  }

  for (int shown = 0; shown < visibleCount(*this); ++shown) {
    const int index = actual(*this, shown);
    const int column = shown % (SETTINGS.hiddenMenu != 0 ? hiddenDrawerGridColumns : gridColumns);
    const int row = shown / (SETTINGS.hiddenMenu != 0 ? hiddenDrawerGridColumns : gridColumns);
    const int itemX = cellX(menuRenderer, column);
    const int itemY = cellY(menuRenderer, row) + (SETTINGS.hiddenMenu != 0 ? 0 : 10);
    const bool active = selectedVisible && shown == selected;

    if (SETTINGS.hiddenMenu != 0) {
      const int itemCellWidth = hiddenCellWidth(menuRenderer);
      const int labelFont = MONTSERRAT_10_FONT_ID;
      const int labelLineHeight = menuRenderer.text.getLineHeight(labelFont);
      const int groupWidth = hiddenDrawerGroupWidth(menuRenderer);
      const int groupX = itemX + (itemCellWidth - groupWidth) / 2;
      const int iconY = itemY + (hiddenDrawerRowHeight - hiddenDrawerIconSize) / 2;
      const int labelY = itemY + (hiddenDrawerRowHeight - labelLineHeight) / 2;
      if (active) {
        menuRenderer.rectangle.fill(itemX, itemY, itemCellWidth, hiddenDrawerRowHeight, true, true, false);
      }
      menuRenderer.bitmap.icon(items[index].icon, groupX, iconY, hiddenDrawerIconSize, hiddenDrawerIconSize,
                               BitmapRender::Orientation::None, active);
      menuRenderer.text.render(labelFont, groupX + hiddenDrawerIconSize + hiddenDrawerIconLabelGap, labelY,
                               items[index].label, !active, EpdFontFamily::REGULAR);
    } else {
      if (active) menuRenderer.rectangle.fill(itemX, itemY, cellSize, cellSize, true);
      const int iconX = itemX + (cellSize - itemSize) / 2;
      const int iconY = itemY + (cellSize - itemSize) / 2;
      menuRenderer.bitmap.icon(items[index].icon, iconX, iconY, itemSize, itemSize,
                               BitmapRender::Orientation::None, active);
    }
  }
  if (SETTINGS.hiddenMenu == 0) {
    menuRenderer.rectangle.render(left, top, popupW, popupH, true);
  }
}

Menu::Action Menu::choose() const {
  open = false;
  const int index = actual(*this, selected);
  return index >= 0 ? items[index].action : Action::None;
}

Menu::Action Menu::handleInput(MappedInputManager& input) const {
#if INX_HAS_FRONTLIGHT || FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO
  const LightDrawer::Action lightAction = lightDrawer.handleInput(input);
  if (lightAction == LightDrawer::Action::Opened) {
    open = false;
    return Action::Opened;
  }
  if (lightAction == LightDrawer::Action::Closed) return Action::Closed;
  if (lightAction == LightDrawer::Action::Adjusted) {
#if FREEINK_DEVICE_PICCO
    // Picco's e-paper refresh is much slower than the X4 Pro/Murphy path.  A
    // slider sample already updates the frontlight and its live row renderer;
    // routing it as Opened sets UiPage::updateRequired and immediately causes
    // a second full drawer render.  That render waits for the Picco panel and
    // drops the next touch samples, making the slider jump or miss values.
    // Keep the input cycle consumed and let the release path perform the one
    // final drawer refresh.
    return Action::None;
#else
    return Action::Opened;
#endif
  }
#endif
  if (SETTINGS.hiddenMenu != 0) {
    if (open && input.wasTouchSwipeDownForRenderer(menuRenderer)) {
      open = false;
      selectedVisible = false;
      return Action::Closed;
    }

    if (!open) {
      float startNx = 0.0f;
      float startNy = 0.0f;
      if (input.wasTouchSwipeUpInScreen(menuRenderer, startNx, startNy)) {
        if (startNy >= hiddenDrawerSwipeStartNy) {
          open = true;
          selected = 0;
          selectedVisible = false;
          return Action::Opened;
        }
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
#if INX_HAS_FRONTLIGHT || FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO
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

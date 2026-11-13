#include "AppsActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <string>
#include <utility>

#include "activity/OpdsServerListActivity.h"
#include "activity/page/HomeSubPage.h"
#include "activity/page/Statistics.h"
#include "activity/settings/StoreActivity.h"
#include "images/BookAtlas.h"
#include "images/BookmarkIcon.h"
#include "images/FavoriteIcon.h"
#include "images/HighlightIcon.h"
#include "images/Search.h"
#include "images/StatisticsIcon.h"
#include "images/Store.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
struct AppItem {
  const char* label;
  const char* subtitle;
  const uint8_t* icon;
};

constexpr AppItem kMenuItems[] = {
    {"Bookmarks", "Browse saved bookmarks", BookmarkIcon},
    {"Highlights", "Review highlighted passages", HighlightIcon},
    {"Favorites", "Open your favorite books", FavoriteIcon},
    {"Statistics", "View reading progress and insights", StatisticsIcon},
    {"Dictionary", "Look up saved words and definitions", BookAtlas},
    {"Store", "Install fonts, languages, dictionaries, and plugins", Store},
    {"OPDS", "Browse books from OPDS catalogs", Search},
};

constexpr int kIconSize = 40;
constexpr int kSideMargin = 20;
constexpr int kIconX = 24;
constexpr int kTextX = 88;
constexpr int kSubtitleGap = 5;
}

// Keep the Apps list aligned with Store's first-row position.
int AppsActivity::bodyTop() { return FREEINK_DEVICE_X4PRO ? 80 : 70; }

int AppsActivity::rowHeight() const {
  return SETTINGS.hiddenMenu == 0 ? kCompactRowHeight : kRowHeight;
}

int AppsActivity::visibleItemCount() const {
  return SETTINGS.disableStatistics != 0 ? kMenuItemCount - 1 : kMenuItemCount;
}

int AppsActivity::itemIndex(const int visibleIndex) const {
  return SETTINGS.disableStatistics != 0 && visibleIndex >= 3 ? visibleIndex + 1 : visibleIndex;
}

void AppsActivity::onEnter() {
  UiPage::onEnter();
  selectedIndex_ = 0;
  subActivityFinished_ = false;
  renderPage();
}

void AppsActivity::onExit() {
  if (subActivity_) {
    subActivity_->onExit();
    subActivity_.reset();
  }
  UiPage::onExit();
}

bool AppsActivity::back() {
  if (onBack_) onBack_();
  return true;
}

void AppsActivity::enterChild(Activity* activity) {
  subActivity_.reset(activity);
  subActivity_->onEnter();
}

void AppsActivity::openSelected() {
  const int item = itemIndex(selectedIndex_);
  const auto close = [this] { subActivityFinished_ = true; };
  switch (item) {
    case 0:
      enterChild(new HomeSubPage(renderer, mappedInput, HomeSubPage::Section::Bookmarks, close));
      break;
    case 1:
      enterChild(new HomeSubPage(renderer, mappedInput, HomeSubPage::Section::Highlights, close));
      break;
    case 2:
      enterChild(new HomeSubPage(renderer, mappedInput, HomeSubPage::Section::Favorites, close));
      break;
    case 3:
      enterChild(new Statistics(renderer, mappedInput, close));
      break;
    case 4:
      enterChild(new HomeSubPage(renderer, mappedInput, HomeSubPage::Section::Dictionary, close));
      break;
    case 5:
      enterChild(new StoreActivity(renderer, mappedInput, close));
      break;
    case 6:
      enterChild(new OpdsServerListActivity(renderer, mappedInput, close));
      break;
    default:
      break;
  }
}

void AppsActivity::loop() {
  if (subActivity_) {
    subActivity_->loop();
    if (subActivityFinished_) {
      subActivity_->onExit();
      subActivity_.reset();
      subActivityFinished_ = false;
      updateRequired = true;
      renderPage();
    }
    return;
  }

  if (menuInput()) return;

  if (mappedInput.hasTouch()) {
    float nx = 0.0f;
    float ny = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, nx, ny)) {
      const int y = static_cast<int>(ny * renderer.getScreenHeight());
      const int top = bodyTop();
      const int currentRowHeight = rowHeight();
      if (y >= top && y < top + visibleItemCount() * currentRowHeight) {
        selectedIndex_ = (y - top) / currentRowHeight;
        openSelected();
      }
      renderPage();
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedIndex_ = (selectedIndex_ + visibleItemCount() - 1) % visibleItemCount();
    updateRequired = true;
    renderPage();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedIndex_ = (selectedIndex_ + 1) % visibleItemCount();
    updateRequired = true;
    renderPage();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    openSelected();
    return;
  }

  renderPage();
}

void AppsActivity::content() {
  const int top = bodyTop();
  const int titleFont = systemFontId();
  const int subtitleFont = systemFontIdForPointSize(8);
  const int screenW = renderer.getScreenWidth();
  const int maxTextWidth = std::max(1, screenW - kTextX - kSideMargin);
  const int itemCount = visibleItemCount();
  const int currentRowHeight = rowHeight();
  const int titleHeight = renderer.text.getLineHeight(titleFont);
  const int subtitleHeight = renderer.text.getLineHeight(subtitleFont);
  const int contentHeight = titleHeight + kSubtitleGap + subtitleHeight;

  for (int visible = 0; visible < itemCount; ++visible) {
    const AppItem& item = kMenuItems[itemIndex(visible)];
    const int y = top + visible * currentRowHeight;
    const int titleY = y + (currentRowHeight - contentHeight) / 2;
    renderer.bitmap.icon(item.icon, kIconX, y + (currentRowHeight - kIconSize) / 2, kIconSize, kIconSize);
    // Keep every title visible; the Store list renders labels in the same ink mode.
    renderer.text.render(titleFont, kTextX, titleY, item.label, true, EpdFontFamily::REGULAR);
    const std::string subtitle = renderer.text.truncate(subtitleFont, item.subtitle, maxTextWidth);
    renderer.text.render(subtitleFont, kTextX, titleY + titleHeight + kSubtitleGap, subtitle.c_str(), true,
                         EpdFontFamily::REGULAR);
    if (visible + 1 < itemCount) {
      renderer.line.render(kSideMargin, y + currentRowHeight - 1, screenW - kSideMargin,
                           y + currentRowHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }

  mappedInput.mapLabels("\xC2\xAB Back", "Open", "Up", "Down");
}

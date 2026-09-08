#include "StoreActivity.h"

#include <GfxRenderer.h>

#include "activity/page/SubPage.h"
#include "activity/settings/FontManagerActivity.h"
#include "activity/settings/LanguageManagerActivity.h"
#include "images/BookAtlas.h"
#include "images/Language.h"
#include "images/Plugins.h"
#include "images/PresetFont.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
struct StoreItem {
  const char* label;
  const char* subtitle;
  const uint8_t* icon;
};

constexpr StoreItem kMenuItems[] = {
    {"Font", "Browse and install reading fonts", PresetFont},
    {"Language", "Add translations and system languages", Language},
    {"Dictionary", "Browse and install dictionaries", BookAtlas},
    {"Plugins", "Explore optional reading extensions", Plugins},
};
constexpr int kIconSize = 40;
constexpr int kSideMargin = 20;
constexpr int kIconX = 24;
constexpr int kTextX = 88;
constexpr int kSubtitleGap = 5;
}

int StoreActivity::bodyTop() { return FREEINK_DEVICE_X4PRO ? 80 : 70; }

void StoreActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  selectedIndex_ = 0;
  subActivityFinished_ = false;
  render();
}

void StoreActivity::openSelected() {
  if (selectedIndex_ == 0) {
    enterNewActivity(new FontManagerActivity(renderer, mappedInput, [this] { subActivityFinished_ = true; }));
  } else if (selectedIndex_ == 1) {
    enterNewActivity(new LanguageManagerActivity(renderer, mappedInput, [this] { subActivityFinished_ = true; }));
  }
  // Dictionary is intentionally a no-op until its store integration is available.
}

void StoreActivity::loop() {
  if (subActivity) {
    ActivityWithSubactivity::loop();
    if (subActivityFinished_) {
      exitActivity();
      subActivityFinished_ = false;
      render();
    }
    return;
  }

  if (SubPage::closeInput(renderer, mappedInput, onBack_, false)) return;

  if (mappedInput.hasTouch() &&
      (mappedInput.wasTouchSwipeUpForRenderer(renderer) || mappedInput.wasTouchSwipeDownForRenderer(renderer))) {
    return;
  }

  if (mappedInput.hasTouch()) {
    float nx = 0.0f;
    float ny = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, nx, ny)) {
      const int y = static_cast<int>(ny * renderer.getScreenHeight());
      const int top = bodyTop();
      if (y >= top && y < top + kMenuItemCount * kRowHeight) {
        selectedIndex_ = (y - top) / kRowHeight;
        openSelected();
      }
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedIndex_ = (selectedIndex_ + kMenuItemCount - 1) % kMenuItemCount;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedIndex_ = (selectedIndex_ + 1) % kMenuItemCount;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    openSelected();
    return;
  }
}

void StoreActivity::render() {
  renderer.clearScreen();
  const int top = SubPage::header(renderer, "Store");
  const int font = systemFontId();
  const int screenW = renderer.getScreenWidth();
  const int firstRowY = top;
  const int subtitleFont = MONTSERRAT_8_FONT_ID;
  const int contentHeight = renderer.text.getLineHeight(font) + kSubtitleGap +
                            renderer.text.getLineHeight(subtitleFont);
  for (int i = 0; i < kMenuItemCount; ++i) {
    const int y = firstRowY + i * kRowHeight;
    const int nextY = y + kRowHeight;
    const int rowHeight = kRowHeight;
    renderer.bitmap.icon(kMenuItems[i].icon, kIconX, y + (rowHeight - kIconSize) / 2, kIconSize, kIconSize);
    const int titleY = y + (rowHeight - contentHeight) / 2;
    renderer.text.render(font, kTextX, titleY, kMenuItems[i].label, true, EpdFontFamily::BOLD);
    const int subtitleY = titleY + renderer.text.getLineHeight(font) + kSubtitleGap;
    renderer.text.render(subtitleFont, kTextX, subtitleY, kMenuItems[i].subtitle, true, EpdFontFamily::REGULAR);
    if (i + 1 < kMenuItemCount) {
      renderer.line.render(kSideMargin, nextY - 1, screenW - kSideMargin, nextY - 1, true,
                           LineRender::Style::Dotted);
    }
  }
  mappedInput.mapLabels("\xC2\xAB Back", "Open", "Up", "Down");
  renderer.displayBuffer();
}

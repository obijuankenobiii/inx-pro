/**
 * @file Settings.cpp
 * @brief Settings page and embedded settings panels.
 */

#include "Settings.h"

#include <cstdio>
#include <GfxRenderer.h>
#include <string>
#include <vector>

#include "activity/page/navigation/Menu.h"
#include "activity/page/components/global/Button.h"
#include "activity/settings/CategorySettingsActivity.h"
#include "activity/settings/ReaderPresetsActivity.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

extern void onGoToHome();
extern void onGoToLibrary(const std::string& path);
extern void onGoToFileTransfer();
extern void onGoToStatistics();

namespace {

constexpr int panelTabY = navigation::Menu::height + 10;
constexpr int panelTabHeight = Button::height + 25;
constexpr int panelTabCount = 3;

int panelTabHorizontalPadding() {
  return 20;
}

int panelTabX(const GfxRenderer& renderer, const int index) {
  return renderer.getScreenWidth() * index / panelTabCount;
}

void renderPanelTab(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                    const char* label, const int alignment, const bool selected) {
  const int font = systemFontId();
  const int horizontalPadding = panelTabHorizontalPadding();
  const std::string shown = renderer.text.truncate(font, label ? label : "",
                                                   std::max(1, width - horizontalPadding * 2));
  const EpdFontFamily::Style style = selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int textWidth = renderer.text.getUntranslatedWidth(font, shown.c_str(), style);
  const int textY = y + (height - renderer.text.getLineHeight(font)) / 2;
  const int textX = alignment == 0       ? x + horizontalPadding
                    : alignment == 2 ? x + width - horizontalPadding - textWidth
                                     : x + (width - textWidth) / 2;
  renderer.text.renderUntranslated(font, textX, textY, shown.c_str(), true, style);

  if (selected) {
    renderer.rectangle.fill(textX, y + height - 3, std::max(1, textWidth), 2, true);
  }
}

std::vector<SettingInfo> buildSystemSettings() {
  std::vector<SettingInfo> settings;
  settings.reserve(40);

  FontManager::scanSDFonts("/fonts", false);

  settings.push_back(SettingInfo::Separator("Display ", GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Toggle("Sidebar Menu", &SystemSetting::hiddenMenu,
                                         GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Enum(
      "Sleep Screen", &SystemSetting::sleepScreen,
      {"Dark", "Light", "Custom", "Recent Book", "Transparent", "None", "Widget"},
      {SystemSetting::DARK, SystemSetting::LIGHT, SystemSetting::CUSTOM, SystemSetting::COVER,
       SystemSetting::TRANSPARENT, SystemSetting::BLANK, SystemSetting::WIDGET},
      GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Action("Choose sleep image", GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Enum("Hide Battery %", &SystemSetting::hideBatteryPercentage,
                                       {"Never", "In Reader", "Always"}, GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Enum("Keyboard", &SystemSetting::keyboardLayout, {"QWERTY", "Numpad"},
                                       GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Enum("System font", &SystemSetting::systemFontFamily,
                                       FontManager::readerFontFamilyEnumLabels(), GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Enum("Text size", &SystemSetting::systemTextSize, {"Small", "Medium", "Large"},
                                       GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Toggle("Hide title for thumbnails", &SystemSetting::hideThumbnailTitles,
                                         GroupType::DEVICE_DISPLAY));

  settings.push_back(SettingInfo::Separator("Clock", GroupType::CLOCK));
  settings.push_back(SettingInfo::Action("Face", GroupType::CLOCK));
  settings.push_back(
      SettingInfo::Enum("Format", &SystemSetting::sleepClockTimeFormat, {"12 hour", "24 hour"}, GroupType::CLOCK));
  settings.push_back(SettingInfo::Toggle("Hide", &SystemSetting::hideMenuClock, GroupType::CLOCK));
  settings.push_back(SettingInfo::Action("Sync", GroupType::CLOCK));

  settings.push_back(SettingInfo::Separator("Image", GroupType::IMAGE));
  settings.push_back(
      SettingInfo::Enum("Cover Mode", &SystemSetting::sleepScreenCoverMode, {"Fill", "Crop"}, GroupType::IMAGE));
  settings.push_back(SettingInfo::Enum("Cover Filter", &SystemSetting::sleepScreenCoverFilter,
                                       {"None", "Contrast", "Inverted"}, GroupType::IMAGE));
  settings.push_back(SettingInfo::Enum("Sleep Image Quality", &SystemSetting::sleepImageQuality,
                                       {"Low", "Medium", "High"}, GroupType::IMAGE));
  settings.push_back(SettingInfo::Enum("Thumbnail corners", &SystemSetting::bitmapRoundedCorners,
                                       {"Square", "Rounded", "Subtle"}, GroupType::IMAGE));

  settings.push_back(SettingInfo::Separator("Theme", GroupType::THEME));

  settings.push_back(SettingInfo::Separator("Device ", GroupType::DEVICE_ADVANCED));
  settings.push_back(SettingInfo::Enum("Time to Sleep", &SystemSetting::sleepTimeout,
                                       {"1 min", "5 min", "10 min", "15 min", "30 min", "Never"},
                                       GroupType::DEVICE_ADVANCED));
  settings.push_back(SettingInfo::Enum("Boot Mode", &SystemSetting::bootSetting, {"Recent Book", "Home"},
                                       GroupType::DEVICE_ADVANCED));
#if FREEINK_DEVICE_X4PRO
  settings.push_back(SettingInfo::Enum("Home Button", &SystemSetting::x4proHomeButton, {"Back", "Home"},
                                       {SystemSetting::X4PRO_HOME_BUTTON_BACK,
                                        SystemSetting::X4PRO_HOME_BUTTON_HOME},
                                       GroupType::DEVICE_ADVANCED));
#endif
  settings.push_back(SettingInfo::Action("Language", GroupType::DEVICE_ADVANCED));
#if FREEINK_DEVICE_STICKY || FREEINK_DEVICE_PICCO
  settings.push_back(SettingInfo::Enum("Flick page turn", &SystemSetting::shakePageTurn,
                                       {"Off", "Normal", "Inverted"}, GroupType::DEVICE_ADVANCED));
  settings.push_back(SettingInfo::Enum("Flick sensitivity", &SystemSetting::shakePageTurnSensitivity,
                                       {"Low", "Normal", "High"}, GroupType::DEVICE_ADVANCED));
#endif
  settings.push_back(SettingInfo::Enum("Power Button", &SystemSetting::shortPwrBtn,
                                       {"Sleep", "Refresh"},
                                       {SystemSetting::SHORT_PWRBTN::SLEEP,
                                        SystemSetting::SHORT_PWRBTN::PAGE_REFRESH},
                                       GroupType::DEVICE_ADVANCED));
  settings.push_back(SettingInfo::Toggle("Turn off statistics", &SystemSetting::disableStatistics,
                                         GroupType::DEVICE_ADVANCED));
#if FREEINK_DEVICE_PICCO
  settings.push_back(SettingInfo::Enum("Toggle", &SystemSetting::piccoToggleMode, {"Light", "Power"},
                                       GroupType::DEVICE_ADVANCED));
#endif

  settings.push_back(SettingInfo::Separator("Actions", GroupType::DEVICE_ACTIONS));
  settings.push_back(SettingInfo::Action("Delete Cache", GroupType::DEVICE_ACTIONS));
  settings.push_back(SettingInfo::Action("Generate Metadata", GroupType::DEVICE_ACTIONS));
  return settings;
}

const char* panelBackLabel(const SettingsPanel panel) {
  return panel == SettingsPanel::System ? "\xC2\xAB Reader" : "\xC2\xAB System";
}

}

Settings::Settings(GfxRenderer& renderer, MappedInputManager& mappedInput) : UiPage("Settings", renderer, mappedInput) {}

void Settings::onEnter() {
  UiPage::onEnter();
  currentPanel = SettingsPanel::System;
  nextPanel = SettingsPanel::System;
  pending = Pending::None;
  externalNavigation = nullptr;
  openPanel();
}

void Settings::onExit() {
  closePanel();
  UiPage::onExit();
}

void Settings::loop() {
#if FREEINK_DEVICE_PICCO || FREEINK_DEVICE_MURPHY_M4
  // Picco and Murphy can report a screen bottom-to-top gesture as raw left.
  // MappedInputManager has already converted it to screen-up; consume that
  // edge gesture before the menu or embedded list can reinterpret it.
  if (mappedInput.hasTouch()) {
    float swipeStartNx = 0.0f;
    float swipeStartNy = 0.0f;
    if (mappedInput.wasTouchSwipeUpInScreen(renderer, swipeStartNx, swipeStartNy) && swipeStartNy >= 0.95f) {
      if (panelDetailOpen() && panel) {
        panel->loop();
      } else {
        back();
      }
      return;
    }
  }
#endif

  // The shared page shell normally consumes swipes for navigation before an
  // embedded panel sees them. Let the presets panel own its bottom-edge close
  // gesture first; ordinary swipes still go through the shared menu.
  bool routedPresetCloseGesture = false;
  if (!panelDetailOpen() && currentPanel == SettingsPanel::Presets && presetsPanel) {
    float swipeStartNx = 0.0f;
    float swipeStartNy = 0.0f;
    if (mappedInput.wasTouchSwipeUpInScreen(renderer, swipeStartNx, swipeStartNy) && swipeStartNy >= 0.95f) {
      presetsPanel->loop();
      routedPresetCloseGesture = true;
    }
  }

  if (!routedPresetCloseGesture && !panelDetailOpen() && UiPage::menuInput()) return;

  if (!panelDetailOpen() && panelTabsInput()) return;

  if (panel) {
    if (!routedPresetCloseGesture) panel->loop();
    if (runExternalNavigation()) return;

    if (panelDetailOpen()) {
      if (categoryPanel && categoryPanel->takeRenderRequest()) updateRequired = true;
      if (readerPanel && readerPanel->takeRenderRequest()) updateRequired = true;
      if (presetsPanel && presetsPanel->takeRenderRequest()) updateRequired = true;
      renderPage();
      return;
    }

    if (categoryPanel && categoryPanel->takeRenderRequest()) updateRequired = true;
    if (readerPanel && readerPanel->takeRenderRequest()) updateRequired = true;
    if (presetsPanel && presetsPanel->takeRenderRequest()) updateRequired = true;

    processPending();
    renderPage();
    return;
  }

  if (pending != Pending::None) {
    processPending();
    renderPage();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    requestPanelSwap();
    processPending();
    renderPage();
  }
}

void Settings::openPanel() {
  closePanel();

  if (currentPanel == SettingsPanel::Reader || currentPanel == SettingsPanel::Presets) {
    auto* reader = new ReaderPresetsActivity(
        renderer, mappedInput, [this] { requestPanelSwap(); },
        [this] { deferExternalNavigation([] { onGoToHome(); }); },
        [this] { deferExternalNavigation([] { onGoToLibrary("/"); }); },
        [this] { deferExternalNavigation([] { onGoToFileTransfer(); }); },
        [this] { deferExternalNavigation([] { onGoToStatistics(); }); }, true,
        currentPanel == SettingsPanel::Presets,
        [this] { deferExternalNavigation([] { onGoToHome(); }); });
    if (currentPanel == SettingsPanel::Reader) {
      readerPanel = reader;
    } else {
      presetsPanel = reader;
    }
    panel.reset(reader);
  } else {
    auto* category = new CategorySettingsActivity(
        renderer, mappedInput, "System settings", buildSystemSettings(), [this] { requestPanelSwap(); },
        nullptr, panelBackLabel(currentPanel),
        [this] { deferExternalNavigation([] { onGoToHome(); }); },
        [this] { deferExternalNavigation([] { onGoToLibrary("/"); }); },
        [this] { deferExternalNavigation([] { onGoToFileTransfer(); }); },
        [this] { deferExternalNavigation([] { onGoToStatistics(); }); }, true,
        [this] { deferExternalNavigation([] { onGoToHome(); }); });
    categoryPanel = category;
    panel.reset(category);
  }

  panel->onEnter();
}

void Settings::closePanel() {
  if (!panel) return;
  panel->onExit();
  panel.reset();
  categoryPanel = nullptr;
  readerPanel = nullptr;
  presetsPanel = nullptr;
}

void Settings::requestPanelSwap() {
  nextPanel = currentPanel == SettingsPanel::System
                  ? SettingsPanel::Reader
                  : currentPanel == SettingsPanel::Reader ? SettingsPanel::System : SettingsPanel::Reader;
  pending = Pending::SwapPanel;
}

void Settings::selectPanel(const SettingsPanel panel) {
  if (panel == currentPanel) {
    updateRequired = true;
    return;
  }
  nextPanel = panel;
  pending = Pending::SwapPanel;
}

void Settings::processPending() {
  const Pending action = pending;
  pending = Pending::None;

  if (action == Pending::SwapPanel) {
    currentPanel = nextPanel;
    openPanel();
    updateRequired = true;
    return;
  }

}

void Settings::deferExternalNavigation(const std::function<void()>& action) { externalNavigation = action; }

bool Settings::runExternalNavigation() {
  if (!externalNavigation) return false;
  const std::function<void()> action = std::move(externalNavigation);
  externalNavigation = nullptr;
  action();
  return true;
}

bool Settings::panelTabsInput() {
  if (!mappedInput.hasTouch()) return false;

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) return false;

  const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
  const bool inHeader = tapY >= panelTabY && tapY < panelTabY + panelTabHeight;
  if (!inHeader) {
    mappedInput.restoreTouchTapInScreen(renderer, tapNx, tapNy);
    return false;
  }

  const int tappedTab = std::min(panelTabCount - 1, tapX * panelTabCount / renderer.getScreenWidth());
  if (tappedTab == 0) {
    selectPanel(SettingsPanel::System);
  } else if (tappedTab == 1) {
    selectPanel(SettingsPanel::Reader);
  } else {
    selectPanel(SettingsPanel::Presets);
  }

  if (pending != Pending::None) processPending();
  renderPage();
  return true;
}

void Settings::panelTabs() {
  const int screenWidth = renderer.getScreenWidth();
  renderer.line.render(0, panelTabY + panelTabHeight - 1, screenWidth - 1, panelTabY + panelTabHeight - 1, true,
                       LineRender::Style::Dotted);
  for (int index = 0; index < panelTabCount; ++index) {
    const int x = panelTabX(renderer, index);
    const int width = panelTabX(renderer, index + 1) - x;
    renderPanelTab(renderer, x, panelTabY, width, panelTabHeight,
                   index == 0 ? "System" : index == 1 ? "Reader" : "Presets",
                   index,
                   (index == 0 && currentPanel == SettingsPanel::System) ||
                       (index == 1 && currentPanel == SettingsPanel::Reader) ||
                       (index == 2 && currentPanel == SettingsPanel::Presets));
  }
}

void Settings::content() {
  if (panelDetailOpen()) {
    if (categoryPanel) categoryPanel->renderEmbedded();
    if (readerPanel) readerPanel->renderEmbedded();
    if (presetsPanel) presetsPanel->renderEmbedded();
    if ((readerPanel && readerPanel->isPopupOpen()) || (presetsPanel && presetsPanel->isPopupOpen())) {
      panelTabs();
    }
    return;
  }

  if (categoryPanel) {
    categoryPanel->renderEmbedded();
  } else if (readerPanel) {
    readerPanel->renderEmbedded();
  } else if (presetsPanel) {
    presetsPanel->renderEmbedded();
  }
  panelTabs();
}

void Settings::menu() {
  if (!panelDetailOpen() || (readerPanel && readerPanel->isPopupOpen()) ||
      (presetsPanel && presetsPanel->isPopupOpen())) {
    UiPage::menu();
  }
}

bool Settings::back() {
  onGoToHome();
  return true;
}

bool Settings::panelDetailOpen() const {
  return (categoryPanel && categoryPanel->isDetailOpen()) || (readerPanel && readerPanel->isDetailOpen()) ||
         (presetsPanel && presetsPanel->isDetailOpen());
}

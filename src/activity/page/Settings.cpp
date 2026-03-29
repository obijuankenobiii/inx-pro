/**
 * @file Settings.cpp
 * @brief Settings page and embedded settings panels.
 */

#include "Settings.h"

#include <cmath>
#include <GfxRenderer.h>
#include <vector>

#include "activity/page/navigation/Menu.h"
#include "activity/page/components/global/Button.h"
#include "activity/settings/CategorySettingsActivity.h"
#include "activity/settings/ReaderPresetsActivity.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

extern void onGoToHome();
extern void onGoToLibrary(const std::string& path);
extern void onGoToFileTransfer();
extern void onGoToStatistics();

namespace {

constexpr int panelTabY = navigation::Menu::height + 20;
constexpr int panelTabHeight = Button::height - 10;
constexpr int panelTabWidth = 120;
constexpr int panelTabGap = 0;
constexpr int panelTabRight = 20;

int panelPresetsX(const GfxRenderer& renderer) {
  return renderer.getScreenWidth() - panelTabRight - panelTabWidth;
}

int panelReaderX(const GfxRenderer& renderer) {
  return panelPresetsX(renderer) - panelTabWidth;
}

int panelSystemX(const GfxRenderer& renderer) {
  return panelReaderX(renderer) - panelTabGap - panelTabWidth;
}

void renderPanelTab(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                    const char* label, const bool selected, const bool roundLeft, const bool roundRight) {
  // Draw a square tab first, then erase only the pixels outside the requested
  // outer corner arcs. This avoids adding a second stroke on either inner edge.
  constexpr int corner = 4;
  const int tone = selected ? static_cast<int>(GfxRenderer::FillTone::Ink)
                            : static_cast<int>(GfxRenderer::FillTone::Paper);

  renderer.rectangle.fill(x, y, width, height, tone, false);
  renderer.rectangle.render(x, y, width, height, true, false);

  auto clearOutsideCorner = [&](const int cornerX, const int cornerY, const bool left, const bool top) {
    for (int dy = 0; dy <= corner; ++dy) {
      for (int dx = 0; dx <= corner; ++dx) {
        const int distanceX = left ? dx - corner : dx;
        const int distanceY = top ? dy - corner : dy;
        if (distanceX * distanceX + distanceY * distanceY > corner * corner) {
          renderer.drawPixel(cornerX + dx, cornerY + dy, false);
        }
      }
    }

    for (int offset = 0; offset <= corner; ++offset) {
      const int span = static_cast<int>(std::sqrt(corner * corner - (corner - offset) * (corner - offset)));
      const int arcX = left ? cornerX + corner - span : cornerX + span;
      const int arcY = top ? cornerY + offset : cornerY + corner - offset;
      renderer.drawPixel(arcX, arcY, true);
    }
  };

  if (roundLeft) {
    clearOutsideCorner(x, y, true, true);
    clearOutsideCorner(x, y + height - corner - 1, true, false);
  }
  if (roundRight) {
    clearOutsideCorner(x + width - corner - 1, y, false, true);
    clearOutsideCorner(x + width - corner - 1, y + height - corner - 1, false, false);
  }

  const int font = systemFontId();
  const int textWidth = renderer.text.getWidth(font, label ? label : "");
  const int textY = y + (height - renderer.text.getLineHeight(font)) / 2;
  const int textX = x + (width - textWidth) / 2;
  renderer.text.render(font, textX, textY, label ? label : "", !selected, EpdFontFamily::REGULAR);
}

std::vector<SettingInfo> buildSystemSettings() {
  std::vector<SettingInfo> settings;
  settings.reserve(40);

  settings.push_back(SettingInfo::Separator("Display ", GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Enum(
      "Sleep Screen", &SystemSetting::sleepScreen,
      {"Dark", "Light", "Custom", "Recent Book", "Transparent Cover", "None", "Date Time", "Widget"},
      GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Action("Choose sleep image", GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Enum("Hide Battery %", &SystemSetting::hideBatteryPercentage,
                                       {"Never", "In Reader", "Always"}, GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Enum("Keyboard", &SystemSetting::keyboardLayout, {"QWERTY", "Numpad"},
                                       GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Enum("Text size", &SystemSetting::systemTextSize, {"Small", "Medium", "Large"},
                                       GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Toggle("Hide title for thumbnails", &SystemSetting::hideThumbnailTitles,
                                         GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Enum("Thumbnail size", &SystemSetting::thumbnailSize, {"Actual", "Even"},
                                       GroupType::DEVICE_DISPLAY));

  settings.push_back(SettingInfo::Separator("Clock", GroupType::CLOCK));
  settings.push_back(SettingInfo::Action("Face", GroupType::CLOCK));
  settings.push_back(
      SettingInfo::Enum("Format", &SystemSetting::sleepClockTimeFormat, {"12 hour", "24 hour"}, GroupType::CLOCK));
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
                                       {"1 min", "5 min", "10 min", "15 min", "30 min"},
                                       GroupType::DEVICE_ADVANCED));
  settings.push_back(SettingInfo::Enum("Boot Mode", &SystemSetting::bootSetting, {"Recent Book", "Home Page"},
                                       GroupType::DEVICE_ADVANCED));
#if FREEINK_DEVICE_STICKY
  settings.push_back(SettingInfo::Enum("Flick page turn", &SystemSetting::shakePageTurn,
                                       {"Off", "Normal", "Inverted"}, GroupType::DEVICE_ADVANCED));
  settings.push_back(SettingInfo::Enum("Flick sensitivity", &SystemSetting::shakePageTurnSensitivity,
                                       {"Low", "Normal", "High"}, GroupType::DEVICE_ADVANCED));
#endif
  settings.push_back(SettingInfo::Toggle("Short Press Power Button", &SystemSetting::shortPressPowerButton,
                                         GroupType::DEVICE_ADVANCED));

  settings.push_back(SettingInfo::Separator("Actions", GroupType::DEVICE_ACTIONS));
  settings.push_back(SettingInfo::Action("Delete Cache", GroupType::DEVICE_ACTIONS));
  settings.push_back(SettingInfo::Action("Generate thumbnails", GroupType::DEVICE_ACTIONS));
  settings.push_back(SettingInfo::Action("Generate Authors", GroupType::DEVICE_ACTIONS));
  return settings;
}

const char* panelBackLabel(const SettingsPanel panel) {
  return panel == SettingsPanel::System ? "\xC2\xAB Reader" : "\xC2\xAB System";
}

}  // namespace

Settings::Settings(GfxRenderer& renderer, MappedInputManager& mappedInput) : Page("Settings", renderer, mappedInput) {}

void Settings::onEnter() {
  Page::onEnter();
  currentPanel = SettingsPanel::System;
  nextPanel = SettingsPanel::System;
  pending = Pending::None;
  externalNavigation = nullptr;
  openPanel();
}

void Settings::onExit() {
  closePanel();
  Page::onExit();
}

void Settings::loop() {
  if (!panelDetailOpen() && Page::menuInput()) return;

  if (!panelDetailOpen() && panelTabsInput()) return;

  if (panel) {
    panel->loop();
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
    SETTINGS.saveToFile();
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
  const int systemX = panelSystemX(renderer);
  const int readerX = panelReaderX(renderer);
  const int presetsX = panelPresetsX(renderer);

  if (!inHeader) {
    mappedInput.restoreTouchTapInScreen(renderer, tapNx, tapNy);
    return false;
  }

  if (tapX >= systemX && tapX < systemX + panelTabWidth) {
    selectPanel(SettingsPanel::System);
  } else if (tapX >= readerX && tapX < readerX + panelTabWidth) {
    selectPanel(SettingsPanel::Reader);
  } else if (tapX >= presetsX && tapX < presetsX + panelTabWidth) {
    selectPanel(SettingsPanel::Presets);
  } else {
    mappedInput.restoreTouchTapInScreen(renderer, tapNx, tapNy);
    return false;
  }

  if (pending != Pending::None) processPending();
  renderPage();
  return true;
}

void Settings::panelTabs() {
  const int systemX = panelSystemX(renderer);
  const int readerX = panelReaderX(renderer);
  renderPanelTab(renderer, systemX, panelTabY, panelTabWidth, panelTabHeight, "System",
                 currentPanel == SettingsPanel::System, true, false);
  renderPanelTab(renderer, readerX, panelTabY, panelTabWidth, panelTabHeight, "Reader",
                 currentPanel == SettingsPanel::Reader, false, false);
  renderPanelTab(renderer, panelPresetsX(renderer), panelTabY, panelTabWidth, panelTabHeight, "Presets",
                 currentPanel == SettingsPanel::Presets, false, true);
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
    Page::menu();
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

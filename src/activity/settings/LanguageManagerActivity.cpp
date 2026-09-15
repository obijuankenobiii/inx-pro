#include "LanguageManagerActivity.h"

#include <GfxRenderer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "activity/network/WifiSelectionActivity.h"
#include "activity/page/SubPage.h"
#include "activity/page/components/global/Button.h"
#include "activity/page/components/global/Toggle.h"
#include "images/Trash.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/LanguageManager.h"
#include "system/MappedInputManager.h"

namespace {
constexpr uint32_t kDisplayTaskStack = 4096;
constexpr uint32_t kInstallTaskStack = 8192;
constexpr int kBottomMargin = 44;
constexpr int kSideMargin = 20;
constexpr int kActionIconSize = 40;
constexpr int kActionIconGap = 12;

int pageBodyTop() { return FREEINK_DEVICE_X4PRO ? 80 : 70; }

bool contains(const ButtonBounds& bounds, const int x, const int y) {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}

bool contains(const ToggleBounds& bounds, const int x, const int y) {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}

bool isInstalledLanguage(const LanguagePackageManager::Package& package) {
  return package.code.empty() || LanguagePackageManager::isInstalled(package);
}

ButtonBounds actionBounds(const GfxRenderer& renderer) {
  constexpr int width = 180;
  return {(renderer.getScreenWidth() - width) / 2, renderer.getScreenHeight() - kBottomMargin - Button::height, width,
          Button::height};
}

void drawProgressBar(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                     const int percent) {
  renderer.rectangle.render(x, y, width, height, true);
  const int innerWidth = std::max(1, width - 2);
  renderer.rectangle.fill(x + 1, y + 1, innerWidth, height - 2, false);
  const int fillWidth = std::max(0, std::min(innerWidth, innerWidth * percent / 100));
  if (fillWidth > 0) renderer.rectangle.fill(x + 1, y + 1, fillWidth, height - 2, true);
}

void drawScrollBar(const GfxRenderer& renderer, const int x, const int y, const int height, const int total,
                   const int visible, const int offset) {
  if (total <= visible || height <= 0) return;
  constexpr int width = 3;
  const int maxOffset = std::max(1, total - visible);
  const int thumbHeight = std::max(14, height * visible / total);
  const int thumbTravel = std::max(1, height - thumbHeight);
  const int thumbY = y + offset * thumbTravel / maxOffset;
  renderer.rectangle.fill(x, y, width, height, static_cast<int>(GfxRenderer::FillTone::Gray), true);
  renderer.rectangle.fill(x, thumbY, width, thumbHeight, static_cast<int>(GfxRenderer::FillTone::Ink), true);
}
}  // namespace

void LanguageManagerActivity::displayTaskTrampoline(void* param) {
  static_cast<LanguageManagerActivity*>(param)->displayTaskLoop();
}

void LanguageManagerActivity::installTaskTrampoline(void* param) {
  static_cast<LanguageManagerActivity*>(param)->installTaskLoop();
}

void LanguageManagerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  state_ = State::Loading;
  status_.clear();
  packages_.clear();
  selectedIndex_ = 0;
  scrollOffset_ = 0;
  selectedVisible_ = false;
  installingPackageIndex_ = -1;
  progressDownloaded_ = 0;
  progressTotal_ = 0;
  updateRequired_ = false;
  shuttingDown_ = false;
  lastProgressPercent_ = -1;
  lastProgressUpdateMs_ = 0;
  render();
  loadPackages();
}

void LanguageManagerActivity::onExit() {
  shuttingDown_ = true;
  const unsigned long start = millis();
  while (installTaskHandle_ && millis() - start < 1500) vTaskDelay(pdMS_TO_TICKS(10));
  ActivityWithSubactivity::onExit();
  if (installTaskHandle_) {
    vTaskDeleteWithCaps(installTaskHandle_);
    installTaskHandle_ = nullptr;
  }
  if (displayTaskHandle_) {
    vTaskDeleteWithCaps(displayTaskHandle_);
    displayTaskHandle_ = nullptr;
  }
  if (renderingMutex_) {
    vSemaphoreDelete(renderingMutex_);
    renderingMutex_ = nullptr;
  }
}

void LanguageManagerActivity::loadPackages() {
  std::string error;
  std::vector<LanguagePackageManager::Package> available;
  packages_.clear();
  packages_.push_back({"", "English", "", 0});

  const bool storeLoaded = LanguagePackageManager::fetchAvailable(available, error);
  if (storeLoaded) {
    for (const LanguagePackageManager::Package& package : available) packages_.push_back(package);
  }

  // Also show packages copied directly to the SD card, even if they are not
  // listed in the store catalog.
  for (const LanguageManager::LanguageInfo& language : LanguageManager::installedLanguages()) {
    if (language.code.empty()) continue;
    auto existing = std::find_if(packages_.begin(), packages_.end(), [&language](const auto& package) {
      return package.code == language.code;
    });
    if (existing != packages_.end()) {
      existing->name = language.name;
    } else {
      packages_.push_back({language.code, language.name, "", 0});
    }
  }

  // Keep installed languages together at the top. Non-installed store rows
  // remain visible below them as disabled entries.
  std::stable_sort(packages_.begin() + 1, packages_.end(), [](const auto& left, const auto& right) {
    return isInstalledLanguage(left) && !isInstalledLanguage(right);
  });
  state_ = State::Ready;
  if (packages_.size() <= 1 && !storeLoaded) {
    status_ = error.empty() ? "No language packages found." : error;
  } else {
    status_ = "Tap an installed language to enable it.";
  }
  updateDisplay();
}

int LanguageManagerActivity::visibleRowCount(const int bodyTop) const {
  return std::max(1, (renderer.getScreenHeight() - kBottomMargin - bodyTop) / kRowHeight);
}

void LanguageManagerActivity::startInstallation() {
  if (installTaskHandle_ || installingPackageIndex_ < 0 || shuttingDown_) return;
  if (!renderingMutex_) renderingMutex_ = xSemaphoreCreateMutex();
  if (!renderingMutex_) {
    state_ = State::Failed;
    status_ = "Could not start download.";
    updateDisplay();
    return;
  }
  if (!displayTaskHandle_ &&
      xTaskCreatePinnedToCoreWithCaps(&LanguageManagerActivity::displayTaskTrampoline, "LangDisplayTask", kDisplayTaskStack,
                                      this, 1, &displayTaskHandle_, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    state_ = State::Failed;
    status_ = "Could not start download.";
    updateDisplay();
    return;
  }
  updateRequired_ = true;
  if (xTaskCreatePinnedToCoreWithCaps(&LanguageManagerActivity::installTaskTrampoline, "LangInstallTask", kInstallTaskStack,
                                      this, 1, &installTaskHandle_, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    installTaskHandle_ = nullptr;
    state_ = State::Failed;
    status_ = "Could not start download.";
    updateRequired_ = true;
  }
}

void LanguageManagerActivity::installSelected() {
  if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(packages_.size())) return;
  const auto& package = packages_[static_cast<size_t>(selectedIndex_)];
  if (package.code.empty()) {
    if (LanguageManager::setLanguage("")) {
      status_ = "English selected.";
    } else {
      status_ = "Could not select English.";
    }
    updateDisplay();
    return;
  }
  if (LanguagePackageManager::isInstalled(package)) {
    const bool isActive = std::strcmp(LanguageManager::activeLanguageCode(), package.code.c_str()) == 0;
    if (LanguageManager::setLanguage(isActive ? "" : package.code.c_str())) {
      status_ = isActive ? "System language set to English." : "Language selected.";
    } else {
      status_ = "Could not select language.";
    }
    updateDisplay();
    return;
  }
  // Store entries are intentionally disabled here. Installation remains
  // available from the Store flow, while Device > Language only manages
  // packages that are already installed on the SD card.
}

void LanguageManagerActivity::removeSelected() {
  if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(packages_.size())) return;
  if (packages_[static_cast<size_t>(selectedIndex_)].code.empty()) return;
  std::string error;
  if (!LanguagePackageManager::remove(packages_[static_cast<size_t>(selectedIndex_)], error)) {
    status_ = error.empty() ? "Language removal failed." : error;
  } else {
    status_ = "Language removed.";
    if (std::strcmp(LanguageManager::activeLanguageCode(), packages_[static_cast<size_t>(selectedIndex_)].code.c_str()) == 0) {
      LanguageManager::setLanguage("");
    }
    selectedVisible_ = false;
  }
  updateDisplay();
}

void LanguageManagerActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired_) {
      updateRequired_ = false;
      if (renderingMutex_ && xSemaphoreTake(renderingMutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        render();
        xSemaphoreGive(renderingMutex_);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void LanguageManagerActivity::installTaskLoop() {
  const int packageIndex = installingPackageIndex_;
  std::string error;
  bool installed = false;
  if (packageIndex >= 0 && packageIndex < static_cast<int>(packages_.size())) {
    installed = LanguagePackageManager::install(
        packages_[static_cast<size_t>(packageIndex)], error,
        [this](const size_t downloaded, const size_t total) {
          progressDownloaded_ = downloaded;
          progressTotal_ = total;
          const int percent = total > 0 ? static_cast<int>((downloaded * 100) / total) : 0;
          const unsigned long now = millis();
          if (percent != lastProgressPercent_ || now - lastProgressUpdateMs_ >= 750) {
            lastProgressPercent_ = percent;
            lastProgressUpdateMs_ = now;
            updateRequired_ = true;
          }
        });
  }
  if (!shuttingDown_) {
    if (installed && renderingMutex_ && xSemaphoreTake(renderingMutex_, pdMS_TO_TICKS(500)) == pdTRUE) {
      FontManager::scanSDFonts("/fonts", true);
      xSemaphoreGive(renderingMutex_);
    }
    if (installed && packageIndex >= 0 && packageIndex < static_cast<int>(packages_.size())) {
      LanguageManager::setLanguage(packages_[static_cast<size_t>(packageIndex)].code.c_str());
    }
    state_ = installed ? State::Ready : State::Failed;
    status_ = installed ? "Language installed." : (error.empty() ? "Language installation failed." : error);
    selectedVisible_ = false;
    updateRequired_ = true;
  }
  installTaskHandle_ = nullptr;
  vTaskDeleteWithCaps(nullptr);
}

void LanguageManagerActivity::updateDisplay() {
  if (displayTaskHandle_) {
    updateRequired_ = true;
    return;
  }
  render();
}

void LanguageManagerActivity::launchWifiSelection() {
  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
}

void LanguageManagerActivity::onWifiSelectionComplete(const bool connected) {
  exitActivity();
  if (!connected || WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    state_ = State::Failed;
    status_ = "Wi-Fi connection failed. Tap Retry.";
    updateDisplay();
    return;
  }
  installSelected();
}

void LanguageManagerActivity::render() {
  renderer.clearScreen();
  const int bodyTop = SubPage::header(renderer, "Language");
  const int listTop = bodyTop + 20;
  const int font = systemFontId();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  if (state_ == State::Loading) {
    renderer.text.centered(font, screenH / 2 - 16, "Loading languages...", true, EpdFontFamily::BOLD);
    renderer.text.centered(font, screenH / 2 + 18, "Please wait", true, EpdFontFamily::REGULAR);
    mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
    renderer.displayBuffer();
    return;
  }
  if (state_ == State::Downloading) {
    const int centerY = bodyTop + (screenH - bodyTop - 80) / 2;
    renderer.text.centered(font, centerY - 72, "DOWNLOADING LANGUAGE", true, EpdFontFamily::BOLD);
    const int index = installingPackageIndex_;
    const char* name = index >= 0 && index < static_cast<int>(packages_.size())
                           ? packages_[static_cast<size_t>(index)].name.c_str()
                           : "Language";
    renderer.text.centered(font, centerY - 34, name, true, EpdFontFamily::BOLD);
    renderer.text.centered(font, centerY + 4, "Installing language package", true, EpdFontFamily::REGULAR);
    const size_t downloaded = progressDownloaded_;
    const size_t total = progressTotal_;
    const int percent = total > 0 ? std::max(0, std::min(100, static_cast<int>((downloaded * 100) / total))) : 0;
    char percentText[8];
    std::snprintf(percentText, sizeof(percentText), "%d%%", percent);
    constexpr int barHeight = 6;
    constexpr int barWidth = 260;
    const int percentWidth = renderer.text.getWidth(font, percentText);
    const int groupWidth = barWidth + 14 + percentWidth;
    const int groupX = (screenW - groupWidth) / 2;
    drawProgressBar(renderer, groupX, centerY + 36, barWidth, barHeight, percent);
    renderer.text.render(font, groupX + barWidth + 14,
                         centerY + 36 + (barHeight - renderer.text.getLineHeight(font)) / 2, percentText, true,
                         EpdFontFamily::REGULAR);
    mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
    renderer.displayBuffer();
    return;
  }

  if (state_ == State::Ready && !packages_.empty()) {
    const int visibleRows = visibleRowCount(listTop);
    const int maxScroll = std::max(0, static_cast<int>(packages_.size()) - visibleRows);
    scrollOffset_ = std::max(0, std::min(scrollOffset_, maxScroll));
    if (selectedVisible_) {
      if (selectedIndex_ < scrollOffset_) scrollOffset_ = selectedIndex_;
      if (selectedIndex_ >= scrollOffset_ + visibleRows) scrollOffset_ = selectedIndex_ - visibleRows + 1;
    }
    const int end = std::min(static_cast<int>(packages_.size()), scrollOffset_ + visibleRows);
    for (int index = scrollOffset_; index < end; ++index) {
      const int y = listTop + (index - scrollOffset_) * kRowHeight;
      const int textY = y + (kRowHeight - renderer.text.getLineHeight(font)) / 2;
      const int systemToggleRight = screenW - kSideMargin;
      const int deleteIconX = systemToggleRight - Toggle::width - kActionIconGap - kActionIconSize;
      const auto& package = packages_[static_cast<size_t>(index)];
      const bool installed = isInstalledLanguage(package);
      const int maxNameWidth = screenW - (kSideMargin * 2) - kActionIconSize - Toggle::width - kActionIconGap - 20;
      const std::string label = renderer.text.truncate(font, package.name.c_str(), maxNameWidth);
      if (installed) {
        renderer.text.render(font, kSideMargin, textY, label.c_str(), true, EpdFontFamily::REGULAR);
      } else {
        renderer.text.renderGray(font, kSideMargin, textY, label.c_str(), true, EpdFontFamily::REGULAR);
      }
      const int iconY = y + (kRowHeight - kActionIconSize) / 2;
      if (installed) {
        const bool systemLanguage = std::strcmp(LanguageManager::activeLanguageCode(), package.code.c_str()) == 0;
        if (!package.code.empty()) {
          renderer.bitmap.icon(Trash, deleteIconX, iconY, kActionIconSize, kActionIconSize,
                               BitmapRender::Orientation::None, false);
        }
        Toggle::render(renderer, systemToggleRight, y, kRowHeight, systemLanguage, false);
      } else {
        renderer.text.renderGray(font, deleteIconX - 34, textY, "Store", true, EpdFontFamily::REGULAR);
      }
      if (index + 1 < end) renderer.line.render(0, y + kRowHeight - 1, screenW, y + kRowHeight - 1, true,
                                                LineRender::Style::Dotted);
    }
    drawScrollBar(renderer, screenW - 8, listTop, visibleRows * kRowHeight, static_cast<int>(packages_.size()),
                  visibleRows, scrollOffset_);
    mappedInput.mapLabels("\xC2\xAB Back", "Enable", "Up", "Down");
  } else {
    const int centerY = bodyTop + (screenH - bodyTop - 80) / 2;
    renderer.text.centered(font, centerY - 26, status_.c_str(), true, EpdFontFamily::BOLD);
    if (state_ == State::Failed) renderer.text.centered(font, centerY + 24, "Retry", true, EpdFontFamily::REGULAR);
    mappedInput.mapLabels("\xC2\xAB Back", state_ == State::Failed ? "Retry" : "", "", "");
  }
  renderer.displayBuffer();
}

void LanguageManagerActivity::loop() {
  if (subActivity) {
    ActivityWithSubactivity::loop();
    return;
  }
  if (SubPage::closeInput(renderer, mappedInput, goBack_, false)) return;

  if (state_ == State::Ready && !packages_.empty() && mappedInput.hasTouch()) {
    const int listTop = pageBodyTop() + 20;
    const bool swipeUp = mappedInput.wasTouchSwipeUpForRenderer(renderer);
    const bool swipeDown = mappedInput.wasTouchSwipeDownForRenderer(renderer);
    if (swipeUp || swipeDown) {
      const int visibleRows = visibleRowCount(listTop);
      const int maxScroll = std::max(0, static_cast<int>(packages_.size()) - visibleRows);
      if (swipeUp) scrollOffset_ = std::min(maxScroll, scrollOffset_ + std::max(1, visibleRows - 1));
      if (swipeDown) scrollOffset_ = std::max(0, scrollOffset_ - std::max(1, visibleRows - 1));
      updateDisplay();
      return;
    }
    float nx = 0.0f;
    float ny = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, nx, ny)) {
      const int x = static_cast<int>(nx * renderer.getScreenWidth());
      const int y = static_cast<int>(ny * renderer.getScreenHeight());
      const int visibleRows = visibleRowCount(listTop);
      const int tapped = scrollOffset_ + (y - listTop) / kRowHeight;
      if (y >= listTop && y < listTop + visibleRows * kRowHeight && tapped >= 0 &&
          tapped < static_cast<int>(packages_.size())) {
        selectedIndex_ = tapped;
        selectedVisible_ = true;
        const int systemToggleRight = renderer.getScreenWidth() - kSideMargin;
        const int deleteIconX = systemToggleRight - Toggle::width - kActionIconGap - kActionIconSize;
        const ButtonBounds deleteBounds{deleteIconX, listTop + (tapped - scrollOffset_) * kRowHeight, kActionIconSize,
                                        kRowHeight};
        const auto& package = packages_[static_cast<size_t>(tapped)];
        const bool installed = isInstalledLanguage(package);
        if (!installed) return;
        const ToggleBounds systemToggle = Toggle::bounds(systemToggleRight,
                                                         listTop + (tapped - scrollOffset_) * kRowHeight, kRowHeight);
        if (installed && !package.code.empty() && contains(deleteBounds, x, y)) {
          removeSelected();
        } else if (installed && contains(systemToggle, x, y)) {
          installSelected();
        } else {
          installSelected();
        }
      }
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    goBack_();
    return;
  }
  if (state_ == State::Ready && !packages_.empty()) {
    const int total = static_cast<int>(packages_.size());
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      selectedIndex_ = (selectedIndex_ + 1) % total;
      selectedVisible_ = true;
      updateDisplay();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      selectedIndex_ = (selectedIndex_ + total - 1) % total;
      selectedVisible_ = true;
      updateDisplay();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      installSelected();
    }
  } else if (state_ == State::Failed && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    state_ = State::Loading;
    updateDisplay();
    loadPackages();
  }
}

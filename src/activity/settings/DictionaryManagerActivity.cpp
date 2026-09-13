#include "DictionaryManagerActivity.h"

#include <GfxRenderer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>

#include "activity/network/WifiSelectionActivity.h"
#include "activity/page/SubPage.h"
#include "images/Download.h"
#include "images/Trash.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
constexpr uint32_t kDisplayTaskStack = 4096;
constexpr uint32_t kInstallTaskStack = 8192;
constexpr int kBottomMargin = 44;
constexpr int kSideMargin = 20;
constexpr int kActionIconSize = 40;

int pageBodyTop() { return FREEINK_DEVICE_X4PRO ? 80 : 70; }

void drawProgressBar(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                     const int percent) {
  renderer.rectangle.render(x, y, width, height, true);
  const int innerWidth = std::max(1, width - 2);
  renderer.rectangle.fill(x + 1, y + 1, innerWidth, height - 2, false);
  const int fillWidth = std::max(0, std::min(innerWidth, innerWidth * percent / 100));
  if (fillWidth > 0) renderer.rectangle.fill(x + 1, y + 1, fillWidth, height - 2, true);
}
}  // namespace

void DictionaryManagerActivity::displayTaskTrampoline(void* param) {
  static_cast<DictionaryManagerActivity*>(param)->displayTaskLoop();
}

void DictionaryManagerActivity::installTaskTrampoline(void* param) {
  static_cast<DictionaryManagerActivity*>(param)->installTaskLoop();
}

void DictionaryManagerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  state_ = State::Ready;
  status_.clear();
  packages_.clear();
  selectedIndex_ = 0;
  installingPackageIndex_ = -1;
  progressDownloaded_ = 0;
  progressTotal_ = 0;
  updateRequired_ = false;
  shuttingDown_ = false;
  lastProgressPercent_ = -1;
  lastProgressUpdateMs_ = 0;
  loadPackages();
}

void DictionaryManagerActivity::onExit() {
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

void DictionaryManagerActivity::loadPackages() {
  std::string error;
  if (DictionaryPackageManager::fetchAvailable(packages_, error) && !packages_.empty()) {
    status_ = "Tap a dictionary to download it.";
    state_ = State::Ready;
  } else {
    state_ = State::Failed;
    status_ = error.empty() ? "No dictionary packages found." : error;
  }
  render();
}

void DictionaryManagerActivity::startInstallation() {
  if (installTaskHandle_ || installingPackageIndex_ < 0 || shuttingDown_) return;
  if (!renderingMutex_) renderingMutex_ = xSemaphoreCreateMutex();
  if (!renderingMutex_) {
    state_ = State::Failed;
    status_ = "Could not start download.";
    updateDisplay();
    return;
  }
  if (!displayTaskHandle_ &&
      xTaskCreatePinnedToCoreWithCaps(&DictionaryManagerActivity::displayTaskTrampoline, "DictDisplayTask",
                                      kDisplayTaskStack, this, 1, &displayTaskHandle_, 1,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    state_ = State::Failed;
    status_ = "Could not start download.";
    updateDisplay();
    return;
  }
  updateRequired_ = true;
  if (xTaskCreatePinnedToCoreWithCaps(&DictionaryManagerActivity::installTaskTrampoline, "DictInstallTask",
                                      kInstallTaskStack, this, 1, &installTaskHandle_, 0,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    installTaskHandle_ = nullptr;
    state_ = State::Failed;
    status_ = "Could not start download.";
    updateRequired_ = true;
  }
}

void DictionaryManagerActivity::installSelected() {
  if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(packages_.size())) return;
  const auto& package = packages_[static_cast<size_t>(selectedIndex_)];
  if (DictionaryPackageManager::isInstalled(package)) {
    std::string error;
    if (DictionaryPackageManager::remove(package, error)) {
      status_ = "Dictionary removed.";
    } else {
      status_ = error.empty() ? "Dictionary removal failed." : error;
    }
    updateDisplay();
    return;
  }
  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    launchWifiSelection();
    return;
  }
  state_ = State::Downloading;
  status_ = "Downloading and installing...";
  installingPackageIndex_ = selectedIndex_;
  progressDownloaded_ = 0;
  progressTotal_ = 0;
  lastProgressPercent_ = -1;
  lastProgressUpdateMs_ = 0;
  startInstallation();
}

void DictionaryManagerActivity::displayTaskLoop() {
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

void DictionaryManagerActivity::installTaskLoop() {
  const int packageIndex = installingPackageIndex_;
  std::string error;
  bool installed = false;
  if (packageIndex >= 0 && packageIndex < static_cast<int>(packages_.size())) {
    installed = DictionaryPackageManager::install(
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
    state_ = installed ? State::Ready : State::Failed;
    status_ = installed ? "Dictionary installed." : (error.empty() ? "Dictionary installation failed." : error);
    updateRequired_ = true;
  }
  installTaskHandle_ = nullptr;
  vTaskDeleteWithCaps(nullptr);
}

void DictionaryManagerActivity::updateDisplay() {
  if (displayTaskHandle_) {
    updateRequired_ = true;
    return;
  }
  render();
}

void DictionaryManagerActivity::launchWifiSelection() {
  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
}

void DictionaryManagerActivity::onWifiSelectionComplete(const bool connected) {
  exitActivity();
  if (!connected || WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    state_ = State::Failed;
    status_ = "Wi-Fi connection failed. Tap Retry.";
    updateDisplay();
    return;
  }
  installSelected();
}

void DictionaryManagerActivity::render() {
  renderer.clearScreen();
  const int bodyTop = SubPage::header(renderer, "Dictionary Manager");
  const int font = systemFontId();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  if (state_ == State::Downloading) {
    const int centerY = bodyTop + (screenH - bodyTop - 80) / 2;
    renderer.text.centered(font, centerY - 72, "DOWNLOADING DICTIONARY", true, EpdFontFamily::BOLD);
    const int index = installingPackageIndex_;
    const char* name = index >= 0 && index < static_cast<int>(packages_.size())
                           ? packages_[static_cast<size_t>(index)].name.c_str()
                           : "Dictionary";
    renderer.text.centered(font, centerY - 34, name, true, EpdFontFamily::BOLD);
    renderer.text.centered(font, centerY + 4, "Downloading and extracting to SD", true, EpdFontFamily::REGULAR);
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
    const auto& package = packages_[0];
    const int y = pageBodyTop() + 20;
    const int textY = y + (kRowHeight - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, kSideMargin, textY, package.name.c_str(), true, EpdFontFamily::REGULAR);
    const int iconX = screenW - kSideMargin - kActionIconSize;
    const int iconY = y + (kRowHeight - kActionIconSize) / 2;
    const bool installed = DictionaryPackageManager::isInstalled(package);
    if (installed) {
      renderer.bitmap.icon(Trash, iconX, iconY, kActionIconSize, kActionIconSize,
                           BitmapRender::Orientation::None, false);
    } else {
      renderer.bitmap.icon(Download, iconX, iconY, kActionIconSize, kActionIconSize,
                           BitmapRender::Orientation::None, false);
    }
    renderer.line.render(kSideMargin, y + kRowHeight - 1, screenW - kSideMargin, y + kRowHeight - 1, true,
                         LineRender::Style::Dotted);
    if (installed) {
      renderer.text.centered(MONTSERRAT_8_FONT_ID, y + kRowHeight + 20, "Installed — tap to remove", true,
                             EpdFontFamily::REGULAR);
    }
    mappedInput.mapLabels("\xC2\xAB Back", installed ? "Remove" : "Download", "", "");
  } else {
    const int centerY = bodyTop + (screenH - bodyTop - 80) / 2;
    renderer.text.centered(font, centerY - 26, status_.c_str(), true, EpdFontFamily::BOLD);
    if (state_ == State::Failed) renderer.text.centered(font, centerY + 24, "Retry", true, EpdFontFamily::REGULAR);
    mappedInput.mapLabels("\xC2\xAB Back", state_ == State::Failed ? "Retry" : "", "", "");
  }
  renderer.displayBuffer();
}

void DictionaryManagerActivity::loop() {
  if (subActivity) {
    ActivityWithSubactivity::loop();
    return;
  }
  if (SubPage::closeInput(renderer, mappedInput, goBack_, false)) return;

  if (state_ == State::Ready && !packages_.empty() && mappedInput.hasTouch()) {
    float nx = 0.0f;
    float ny = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, nx, ny)) {
      const int y = static_cast<int>(ny * renderer.getScreenHeight());
      const int listTop = pageBodyTop() + 20;
      if (y >= listTop && y < listTop + kRowHeight) installSelected();
      return;
    }
  }

  if (state_ == State::Ready && !packages_.empty() && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    installSelected();
  } else if (state_ == State::Failed && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    state_ = State::Ready;
    loadPackages();
  }
}

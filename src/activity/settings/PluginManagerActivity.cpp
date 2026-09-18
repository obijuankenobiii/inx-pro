#include "PluginManagerActivity.h"

#include <GfxRenderer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>

#include "activity/network/WifiSelectionActivity.h"
#include "activity/page/SubPage.h"
#include "activity/page/components/global/Button.h"
#include "images/Anki.h"
#include "images/Check.h"
#include "images/Download.h"
#include "images/Series.h"
#include "images/Trash.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
constexpr uint32_t kTaskStack = 8192;
constexpr int kRowHeight = 80;
constexpr int kSideMargin = 20;
constexpr int kLogoX = 24;
constexpr int kTextX = 88;
constexpr int kLogoSize = 40;
constexpr int kIconSize = 40;

}  // namespace

void PluginManagerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  state_ = State::Ready;
  status_.clear();
  packages_.clear();
  selectedPackage_ = 0;
  downloaded_ = 0;
  total_ = 0;
  showCompletionCheck_ = false;
  completionCheckExpiresAt_ = 0;
  updateRequired_ = false;
  shuttingDown_ = false;
  lastPercent_ = -1;
  loadPackages();
}

void PluginManagerActivity::onExit() {
  shuttingDown_ = true;
  const unsigned long started = millis();
  while (workerTask_ && millis() - started < 1500) vTaskDelay(pdMS_TO_TICKS(10));
  ActivityWithSubactivity::onExit();
  if (workerTask_) {
    vTaskDeleteWithCaps(workerTask_);
    workerTask_ = nullptr;
  }
  if (displayTask_) {
    vTaskDeleteWithCaps(displayTask_);
    displayTask_ = nullptr;
  }
  if (renderingMutex_) {
    vSemaphoreDelete(renderingMutex_);
    renderingMutex_ = nullptr;
  }
}

void PluginManagerActivity::loadPackages() {
  std::string error;
  if (PluginManager::fetchAvailable(packages_, error) && !packages_.empty()) {
    status_ = "Tap a plugin to install it.";
    state_ = State::Ready;
  } else {
    state_ = State::Failed;
    status_ = error.empty() ? "No plugins available." : error;
  }
  render();
}

void PluginManagerActivity::installSelected() {
  if (packages_.empty() || state_ == State::Downloading) return;
  const auto& package = packages_[std::min(selectedPackage_, packages_.size() - 1)];
  std::string error;
  if (PluginManager::isInstalled(package)) {
    if (PluginManager::remove(package, error)) {
      showCompletionCheck_ = false;
      completionCheckExpiresAt_ = 0;
      status_ = "Plugin disabled. Card data was kept.";
      render();
    } else {
      state_ = State::Failed;
      status_ = error.empty() ? "Plugin removal failed." : error;
      render();
    }
    return;
  }
  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    launchWifiSelection();
    return;
  }
  state_ = State::Downloading;
  status_ = "Downloading and installing...";
  downloaded_ = 0;
  total_ = 0;
  showCompletionCheck_ = false;
  completionCheckExpiresAt_ = 0;
  lastPercent_ = -1;
  startInstallation();
}

void PluginManagerActivity::startInstallation() {
  if (workerTask_ || shuttingDown_) return;
  if (!renderingMutex_) renderingMutex_ = xSemaphoreCreateMutex();
  if (!renderingMutex_) {
    state_ = State::Failed;
    status_ = "Could not start download.";
    render();
    return;
  }
  if (!displayTask_ &&
      xTaskCreatePinnedToCoreWithCaps(&PluginManagerActivity::displayTaskTrampoline, "PluginDisplayTask", 4096, this,
                                      1, &displayTask_, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    state_ = State::Failed;
    status_ = "Could not start download.";
    render();
    return;
  }
  updateRequired_ = true;
  if (xTaskCreatePinnedToCoreWithCaps(&PluginManagerActivity::workerTaskTrampoline, "PluginInstallTask", kTaskStack,
                                      this, 1, &workerTask_, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    workerTask_ = nullptr;
    state_ = State::Failed;
    status_ = "Could not start download.";
    updateRequired_ = true;
  }
}

void PluginManagerActivity::launchWifiSelection() {
  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
}

void PluginManagerActivity::onWifiSelectionComplete(const bool connected) {
  exitActivity();
  if (!connected || WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    state_ = State::Failed;
    status_ = "Wi-Fi connection failed. Tap Retry.";
    render();
    return;
  }
  installSelected();
}

void PluginManagerActivity::workerTaskTrampoline(void* param) {
  static_cast<PluginManagerActivity*>(param)->workerTaskLoop();
}

void PluginManagerActivity::displayTaskTrampoline(void* param) {
  static_cast<PluginManagerActivity*>(param)->displayTaskLoop();
}

[[noreturn]] void PluginManagerActivity::workerTaskLoop() {
  std::string error;
  bool installed = false;
  if (!packages_.empty()) {
    installed = PluginManager::install(packages_[std::min(selectedPackage_, packages_.size() - 1)], error,
                                       [this](const size_t downloaded, const size_t total) {
      downloaded_ = downloaded;
      total_ = total;
      const int percent = total > 0 ? static_cast<int>((downloaded * 100) / total) : 0;
      if (percent != lastPercent_) {
        lastPercent_ = percent;
        updateRequired_ = true;
      }
                                       });
  }
  if (!shuttingDown_) {
    state_ = installed ? State::Ready : State::Failed;
    showCompletionCheck_ = installed;
    completionCheckExpiresAt_ = installed ? millis() + 2000 : 0;
    status_ = installed ? "Plugin installed." : (error.empty() ? "Plugin installation failed." : error);
    updateRequired_ = true;
  }
  workerTask_ = nullptr;
  vTaskDeleteWithCaps(nullptr);
  while (true) vTaskDelay(portMAX_DELAY);
}

[[noreturn]] void PluginManagerActivity::displayTaskLoop() {
  while (true) {
    if (showCompletionCheck_ && completionCheckExpiresAt_ != 0 &&
        static_cast<int32_t>(millis() - completionCheckExpiresAt_) >= 0) {
      showCompletionCheck_ = false;
      completionCheckExpiresAt_ = 0;
      updateRequired_ = true;
    }
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

void PluginManagerActivity::render() {
  renderer.clearScreen();
  const int top = SubPage::header(renderer, "Plugin Manager");
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int font = systemFontId();
  if (state_ == State::Downloading) {
    const auto& package = packages_[std::min(selectedPackage_, packages_.size() - 1)];
    constexpr int listTopGap = 20;
    const int rowY = top + listTopGap;
    const int percent = total_ > 0 ? std::max(0, std::min(100, static_cast<int>((downloaded_ * 100) / total_))) : 0;
    const std::string percentText = std::to_string(percent) + "%";
    const int percentWidth = renderer.text.getWidth(font, percentText.c_str());
    const int descriptionMaxWidth = std::max(1, screenW - kTextX - kSideMargin - percentWidth - 20);
    const std::string description = renderer.text.truncate(
        MONTSERRAT_8_FONT_ID, package.description.c_str(), descriptionMaxWidth, EpdFontFamily::REGULAR);
    const int descriptionFont = MONTSERRAT_8_FONT_ID;
    const int contentHeight = renderer.text.getLineHeight(font) + 5 + renderer.text.getLineHeight(descriptionFont);
    const int titleY = rowY + (kRowHeight - contentHeight) / 2;
    const int descriptionY = titleY + renderer.text.getLineHeight(font) + 5;
    const int maxNameWidth = screenW - kTextX - kSideMargin - percentWidth - 20;
    const std::string name = renderer.text.truncate(font, package.name.c_str(), maxNameWidth);
    renderer.bitmap.icon(package.id == "series" ? Series : Anki, kLogoX, rowY + (kRowHeight - kLogoSize) / 2,
                         kLogoSize, kLogoSize);
    renderer.text.render(font, kTextX, titleY, name.c_str(), true, EpdFontFamily::BOLD);
    renderer.text.render(descriptionFont, kTextX, descriptionY, description.c_str(), true,
                         EpdFontFamily::REGULAR);
    renderer.text.render(font, screenW - kSideMargin - percentWidth,
                         rowY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, percentText.c_str(), true,
                         EpdFontFamily::REGULAR);
    mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
    renderer.displayBuffer();
    return;
  }

  if (!packages_.empty()) {
    const int iconX = screenW - kSideMargin - kIconSize;
    for (size_t index = 0; index < packages_.size(); ++index) {
      const auto& package = packages_[index];
      const int y = top + 20 + static_cast<int>(index) * kRowHeight;
      const bool installed = PluginManager::isInstalled(package);
      const int descriptionMaxWidth = std::max(1, iconX - kTextX - 16);
      const std::string description = renderer.text.truncate(
          MONTSERRAT_8_FONT_ID, package.description.c_str(), descriptionMaxWidth, EpdFontFamily::REGULAR);
      const int descriptionFont = MONTSERRAT_8_FONT_ID;
      const int contentHeight = renderer.text.getLineHeight(font) + 5 + renderer.text.getLineHeight(descriptionFont);
      const int titleY = y + (kRowHeight - contentHeight) / 2;
      const int descriptionY = titleY + renderer.text.getLineHeight(font) + 5;
      renderer.bitmap.icon(package.id == "series" ? Series : Anki, kLogoX, y + (kRowHeight - kLogoSize) / 2,
                           kLogoSize, kLogoSize);
      renderer.text.render(font, kTextX, titleY, package.name.c_str(), true, EpdFontFamily::BOLD);
      renderer.text.render(descriptionFont, kTextX, descriptionY, description.c_str(), true,
                           EpdFontFamily::REGULAR);
      renderer.bitmap.icon(installed && index == selectedPackage_ && showCompletionCheck_ ? Check
                                                                                           : (installed ? Trash : Download),
                           iconX, y + (kRowHeight - kIconSize) / 2, kIconSize, kIconSize);
      if (index + 1 < packages_.size()) {
        renderer.line.render(kSideMargin, y + kRowHeight - 1, screenW - kSideMargin, y + kRowHeight - 1, true,
                             LineRender::Style::Dotted);
      }
    }
    const auto& selected = packages_[std::min(selectedPackage_, packages_.size() - 1)];
    mappedInput.mapLabels("\xC2\xAB Back", PluginManager::isInstalled(selected) ? "Disable" : "Install", "", "");
  } else {
    const int center = top + (screenH - top) / 2;
    renderer.text.centered(font, center, status_.c_str(), true, EpdFontFamily::BOLD);
    mappedInput.mapLabels("\xC2\xAB Back", "Retry", "", "");
  }
  renderer.displayBuffer();
}

void PluginManagerActivity::loop() {
  if (subActivity) {
    ActivityWithSubactivity::loop();
    return;
  }
  if (SubPage::closeInput(renderer, mappedInput, goBack_)) return;
  if (state_ == State::Downloading) return;
  if (mappedInput.hasTouch()) {
    float nx = 0.0f;
    float ny = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, nx, ny)) {
      const int y = static_cast<int>(ny * renderer.getScreenHeight());
      const int top = SubPage::header(renderer, "Plugin Manager");
      if (y >= top + 20 && y < top + 20 + static_cast<int>(packages_.size()) * kRowHeight) {
        const size_t index = static_cast<size_t>((y - top - 20) / kRowHeight);
        if (index < packages_.size()) {
          selectedPackage_ = index;
          installSelected();
        }
      }
      return;
    }
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) && !packages_.empty()) {
    selectedPackage_ = (selectedPackage_ + packages_.size() - 1) % packages_.size();
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down) && !packages_.empty()) {
    selectedPackage_ = (selectedPackage_ + 1) % packages_.size();
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    installSelected();
    return;
  }
  if (state_ == State::Failed && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    loadPackages();
  }
}

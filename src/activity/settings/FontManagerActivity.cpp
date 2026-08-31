#include "FontManagerActivity.h"

#include <GfxRenderer.h>
#include <WiFi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "activity/page/SubPage.h"
#include "activity/network/WifiSelectionActivity.h"
#include "activity/page/components/global/Button.h"
#include "images/Check.h"
#include "images/Download.h"
#include "images/LibraryFilterRight.h"
#include "images/Trash.h"
#include "state/ReaderSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/StringUtils.h"

namespace {
constexpr uint32_t kDisplayTaskStack = 4096;
constexpr uint32_t kInstallTaskStack = 8192;
constexpr int kBottomMargin = 44;
constexpr int kSideMargin = 20;
constexpr int kActionIconSize = 40;
constexpr int kActionIconGap = 12;
constexpr int kScrollCaretSize = 40;
constexpr int kTabWidth = 120;
constexpr int kTabHeight = Button::height - 10;

int pageBodyTop() { return FREEINK_DEVICE_X4PRO ? 80 : 70; }

bool contains(const ButtonBounds& bounds, const int x, const int y) {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
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

ButtonBounds scrollCaretBounds(const GfxRenderer& renderer) {
  return {renderer.getScreenWidth() - kSideMargin - kScrollCaretSize, renderer.getScreenHeight() - kBottomMargin + 2,
          kScrollCaretSize, kScrollCaretSize};
}

void renderVariantTab(const GfxRenderer& renderer, const int x, const int y, const char* label, const bool selected,
                      const bool roundLeft, const bool roundRight) {
  constexpr int corner = 4;
  const int tone = selected ? static_cast<int>(GfxRenderer::FillTone::Ink)
                            : static_cast<int>(GfxRenderer::FillTone::Paper);
  renderer.rectangle.fill(x, y, kTabWidth, kTabHeight, tone, false);
  renderer.rectangle.render(x, y, kTabWidth, kTabHeight, true, false);

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
    clearOutsideCorner(x, y + kTabHeight - corner - 1, true, false);
  }
  if (roundRight) {
    clearOutsideCorner(x + kTabWidth - corner - 1, y, false, true);
    clearOutsideCorner(x + kTabWidth - corner - 1, y + kTabHeight - corner - 1, false, false);
  }

  const int font = systemFontId();
  const int textWidth = renderer.text.getWidth(font, label);
  const int textY = y + (kTabHeight - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, x + (kTabWidth - textWidth) / 2, textY, label, !selected, EpdFontFamily::REGULAR);
}
}  // namespace

void FontManagerActivity::displayTaskTrampoline(void* param) {
  static_cast<FontManagerActivity*>(param)->displayTaskLoop();
}

void FontManagerActivity::installTaskTrampoline(void* param) {
  static_cast<FontManagerActivity*>(param)->installTaskLoop();
}

void FontManagerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  state_ = State::Loading;
  status_.clear();
  selectedIndex_ = 0;
  scrollOffset_ = 0;
  selectedVisible_ = false;
  packages_.clear();
  activeVariant_ = 1;
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

void FontManagerActivity::onExit() {
  shuttingDown_ = true;

  const unsigned long start = millis();
  while (installTaskHandle_ && millis() - start < 1500) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  ActivityWithSubactivity::onExit();

  if (installTaskHandle_) {
    vTaskDelete(installTaskHandle_);
    installTaskHandle_ = nullptr;
  }
  if (displayTaskHandle_) {
    vTaskDelete(displayTaskHandle_);
    displayTaskHandle_ = nullptr;
  }
  if (renderingMutex_) {
    vSemaphoreDelete(renderingMutex_);
    renderingMutex_ = nullptr;
  }
}

void FontManagerActivity::loadPackages() {
  std::string error;
  if (FontPackageManager::fetchAvailable(packages_, error)) {
    state_ = State::Ready;
    status_ = packages_.empty() ? "No font packages found." : "Tap a font to download it.";
  } else {
    state_ = State::Failed;
    status_ = error.empty() ? "Could not load font packages." : error;
  }
  updateDisplay();
}

int FontManagerActivity::visibleRowCount(const int bodyTop) const {
  return std::max(1, (renderer.getScreenHeight() - kBottomMargin - bodyTop) / kRowHeight);
}

int FontManagerActivity::visiblePackageCount() const {
  int count = 0;
  for (const FontPackageManager::Package& package : packages_) {
    if (package.variant == activeVariantName()) ++count;
  }
  return count;
}

int FontManagerActivity::packageIndexAt(const int visibleIndex) const {
  if (visibleIndex < 0) return -1;
  int index = 0;
  for (int packageIndex = 0; packageIndex < static_cast<int>(packages_.size()); ++packageIndex) {
    if (packages_[static_cast<size_t>(packageIndex)].variant != activeVariantName()) continue;
    if (index == visibleIndex) return packageIndex;
    ++index;
  }
  return -1;
}

void FontManagerActivity::selectVariant(const int variant) {
  if (variant < 0 || variant > 1 || variant == activeVariant_) return;
  activeVariant_ = variant;
  selectedIndex_ = 0;
  scrollOffset_ = 0;
  selectedVisible_ = false;
  updateDisplay();
}

void FontManagerActivity::installSelected() {
  const int packageIndex = packageIndexAt(selectedIndex_);
  if (packageIndex < 0) return;

  if (FontPackageManager::isInstalled(packages_[static_cast<size_t>(packageIndex)])) {
    selectInstalled();
    return;
  }

  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    launchWifiSelection();
    return;
  }

  state_ = State::Downloading;
  status_ = "Downloading and installing...";
  installingPackageIndex_ = packageIndex;
  progressDownloaded_ = 0;
  progressTotal_ = 0;
  lastProgressPercent_ = -1;
  lastProgressUpdateMs_ = 0;
  startInstallation();
}

void FontManagerActivity::selectInstalled() {
  const int packageIndex = packageIndexAt(selectedIndex_);
  if (packageIndex < 0) return;

  const FontPackageManager::Package& package = packages_[static_cast<size_t>(packageIndex)];
  const std::string family = StringUtils::sanitizeFilename(package.installFamily, 48);
  const std::vector<std::string> families = FontManager::readerFontFamilyEnumLabels();
  const auto familyIt = std::find(families.begin(), families.end(), family);
  if (familyIt == families.end()) {
    status_ = "Installed font is unavailable.";
    selectedVisible_ = false;
    updateDisplay();
    return;
  }

  READER_SETTINGS.fontFamily = static_cast<uint8_t>(std::distance(families.begin(), familyIt));
  READER_SETTINGS.saveToFile();
  status_ = "Font selected.";
  selectedVisible_ = false;
  updateDisplay();
}

void FontManagerActivity::removeSelected() {
  const int packageIndex = packageIndexAt(selectedIndex_);
  if (packageIndex < 0) return;

  const FontPackageManager::Package& package = packages_[static_cast<size_t>(packageIndex)];
  if (!FontPackageManager::isInstalled(package)) return;

  const std::string family = StringUtils::sanitizeFilename(package.installFamily, 48);
  const std::vector<std::string> families = FontManager::readerFontFamilyEnumLabels();
  const bool wasSelectedReaderFont = READER_SETTINGS.fontFamily < families.size() &&
                                     families[READER_SETTINGS.fontFamily] == family;
  std::string error;
  if (!FontPackageManager::remove(package, error)) {
    status_ = error.empty() ? "Font removal failed." : error;
    updateDisplay();
    return;
  }

  if (wasSelectedReaderFont) {
    READER_SETTINGS.fontFamily = SystemSetting::CHAREINK;
    READER_SETTINGS.saveToFile();
  }
  selectedVisible_ = false;
  status_ = "Font removed.";
  updateDisplay();
}

void FontManagerActivity::startInstallation() {
  if (installTaskHandle_ || installingPackageIndex_ < 0 || shuttingDown_) return;

  if (!renderingMutex_) renderingMutex_ = xSemaphoreCreateMutex();
  if (!renderingMutex_) {
    state_ = State::Failed;
    status_ = "Could not start download.";
    updateDisplay();
    return;
  }

  if (!displayTaskHandle_ &&
      xTaskCreatePinnedToCore(&FontManagerActivity::displayTaskTrampoline, "FontDisplayTask", kDisplayTaskStack,
                              this, 1, &displayTaskHandle_, 1) != pdPASS) {
    state_ = State::Failed;
    status_ = "Could not start download.";
    updateDisplay();
    return;
  }

  updateRequired_ = true;
  if (xTaskCreatePinnedToCore(&FontManagerActivity::installTaskTrampoline, "FontInstallTask", kInstallTaskStack, this,
                              1, &installTaskHandle_, 0) != pdPASS) {
    installTaskHandle_ = nullptr;
    state_ = State::Failed;
    status_ = "Could not start download.";
    updateRequired_ = true;
  }
}

void FontManagerActivity::displayTaskLoop() {
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

void FontManagerActivity::installTaskLoop() {
  const int packageIndex = installingPackageIndex_;
  std::string error;
  bool installed = false;
  if (packageIndex >= 0 && packageIndex < static_cast<int>(packages_.size())) {
    installed = FontPackageManager::install(
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
    if (installed) {
      state_ = State::Ready;
      status_ = "Font installed.";
      selectedVisible_ = false;
    } else {
      state_ = State::Failed;
      status_ = error.empty() ? "Font installation failed." : error;
    }
    updateRequired_ = true;
  }
  installTaskHandle_ = nullptr;
  vTaskDelete(nullptr);
}

void FontManagerActivity::updateDisplay() {
  if (displayTaskHandle_) {
    updateRequired_ = true;
    return;
  }

  render();
}

void FontManagerActivity::launchWifiSelection() {
  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
}

void FontManagerActivity::onWifiSelectionComplete(const bool connected) {
  exitActivity();

  if (!connected || WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    state_ = State::Failed;
    status_ = "Wi-Fi connection failed. Tap Retry.";
    updateDisplay();
    return;
  }

  // Continue the download that opened the Wi-Fi flow after the network is ready.
  installSelected();
}

void FontManagerActivity::render() {
  renderer.clearScreen();
  const int bodyTop = SubPage::header(renderer, "Font Manager");
  const int fontListTop = listTop(bodyTop);
  const int font = systemFontId();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  if (state_ == State::Loading) {
    renderer.text.centered(font, screenH / 2 - 16, "Loading fonts...", true, EpdFontFamily::BOLD);
    renderer.text.centered(font, screenH / 2 + 18, "Please wait", true, EpdFontFamily::REGULAR);
    mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
    renderer.displayBuffer();
    return;
  }

  if (state_ == State::Downloading) {
    const int centerY = bodyTop + (screenH - bodyTop - 80) / 2;
    renderer.text.centered(font, centerY - 72, "DOWNLOADING FONT", true, EpdFontFamily::BOLD);
    const int packageIndex = installingPackageIndex_;
    const char* packageName = packageIndex >= 0 && packageIndex < static_cast<int>(packages_.size())
                                  ? packages_[static_cast<size_t>(packageIndex)].name.c_str()
                                  : "Font";
    const std::string name = renderer.text.truncate(font, packageName, screenW - 60);
    renderer.text.centered(font, centerY - 34, name.c_str(), true, EpdFontFamily::BOLD);
    renderer.text.centered(font, centerY + 4, "Installing font package", true, EpdFontFamily::REGULAR);

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
    const int total = visiblePackageCount();
    const int visibleRows = visibleRowCount(fontListTop);
    const int maxScroll = std::max(0, total - visibleRows);
    scrollOffset_ = std::max(0, std::min(scrollOffset_, maxScroll));
    if (selectedVisible_) {
      if (selectedIndex_ < scrollOffset_) scrollOffset_ = selectedIndex_;
      if (selectedIndex_ >= scrollOffset_ + visibleRows) scrollOffset_ = selectedIndex_ - visibleRows + 1;
    }

    const int tabsX = screenW - kSideMargin - kTabWidth * 2;
    for (int tab = 0; tab < 2; ++tab) {
      const int tabX = tabsX + tab * kTabWidth;
      const bool selectedTab = tab == activeVariant_;
      const char* label = tab == 0 ? "1-bit" : "2-bit";
      renderVariantTab(renderer, tabX, bodyTop, label, selectedTab, tab == 0, tab == 1);
    }

    const int end = std::min(total, scrollOffset_ + visibleRows);
    for (int index = scrollOffset_; index < end; ++index) {
      const int packageIndex = packageIndexAt(index);
      if (packageIndex < 0) continue;
      const int y = fontListTop + (index - scrollOffset_) * kRowHeight;
      const bool selected = selectedVisible_ && index == selectedIndex_;
      if (selected) renderer.rectangle.fill(0, y, screenW, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
      const int textY = y + (kRowHeight - renderer.text.getLineHeight(font)) / 2;
      const int deleteIconX = screenW - kSideMargin - kActionIconSize;
      const int actionIconX = deleteIconX - kActionIconGap - kActionIconSize;
      const int maxNameWidth = screenW - (kSideMargin * 2) - (kActionIconSize * 2) - kActionIconGap - 20;
      const std::string packageName = renderer.text.truncate(font, packages_[static_cast<size_t>(packageIndex)].name.c_str(),
                                                              maxNameWidth, EpdFontFamily::REGULAR);
      const bool installed = FontPackageManager::isInstalled(packages_[static_cast<size_t>(packageIndex)]);
      if (installed && !selected) {
        renderer.text.renderGray(font, kSideMargin, textY, packageName.c_str(), true, EpdFontFamily::REGULAR);
      } else {
        renderer.text.render(font, kSideMargin, textY, packageName.c_str(), !selected, EpdFontFamily::REGULAR);
      }
      const int iconY = y + (kRowHeight - kActionIconSize) / 2;
      if (installed) {
        renderer.bitmap.icon(Check, actionIconX, iconY, kActionIconSize, kActionIconSize,
                             BitmapRender::Orientation::None, selected);
        renderer.bitmap.icon(Trash, deleteIconX, iconY, kActionIconSize, kActionIconSize,
                             BitmapRender::Orientation::None, selected);
      } else {
        renderer.bitmap.icon(Download, deleteIconX, iconY, kActionIconSize, kActionIconSize,
                             BitmapRender::Orientation::None, selected);
      }
      if (index + 1 < end) {
        renderer.line.render(0, y + kRowHeight - 1, screenW, y + kRowHeight - 1, true, LineRender::Style::Dotted);
      }
    }
    drawScrollBar(renderer, screenW - 8, fontListTop, visibleRows * kRowHeight, total, visibleRows, scrollOffset_);
    const ButtonBounds caretBounds = scrollCaretBounds(renderer);
    const auto caretOrientation = scrollOffset_ >= maxScroll ? BitmapRender::Orientation::Rotate270CW
                                                               : BitmapRender::Orientation::Rotate90CW;
    renderer.bitmap.iconScaled(LibraryFilterRight, caretBounds.x, caretBounds.y, 30, 30, kScrollCaretSize,
                               kScrollCaretSize, caretOrientation);
    mappedInput.mapLabels("\xC2\xAB Back", "Download", "Up", "Down");
  } else {
    const int centerY = bodyTop + (screenH - bodyTop - 80) / 2;
    renderer.text.centered(font, centerY - 26, status_.c_str(), true, EpdFontFamily::BOLD);
    if (state_ == State::Failed) {
      Button::render(renderer, actionBounds(renderer), "Retry", true, font);
    }
    mappedInput.mapLabels("\xC2\xAB Back", state_ == State::Failed ? "Retry" : "", "", "");
  }
  renderer.displayBuffer();
}

void FontManagerActivity::loop() {
  if (subActivity) {
    ActivityWithSubactivity::loop();
    return;
  }

  if (SubPage::closeInput(renderer, mappedInput, goBack_, false)) return;

  if (state_ == State::Ready && !packages_.empty() && mappedInput.hasTouch()) {
    const int bodyTop = pageBodyTop();
    const int fontListTop = listTop(bodyTop);
    const bool swipeUp = mappedInput.wasTouchSwipeUpForRenderer(renderer);
    const bool swipeDown = mappedInput.wasTouchSwipeDownForRenderer(renderer);
    if (swipeUp || swipeDown) {
      const int visibleRows = visibleRowCount(fontListTop);
      const int maxScroll = std::max(0, visiblePackageCount() - visibleRows);
      if (swipeUp) {
        scrollOffset_ = std::min(maxScroll, scrollOffset_ + std::max(1, visibleRows - 1));
      } else {
        scrollOffset_ = std::max(0, scrollOffset_ - std::max(1, visibleRows - 1));
      }
      updateDisplay();
      return;
    }
  }

  if (mappedInput.hasTouch()) {
    float nx = 0.0f;
    float ny = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, nx, ny)) {
      const int x = static_cast<int>(nx * renderer.getScreenWidth());
      const int y = static_cast<int>(ny * renderer.getScreenHeight());
      if (state_ == State::Ready && !packages_.empty()) {
        const int bodyTop = pageBodyTop();
        const int fontListTop = listTop(bodyTop);
        const int tabsX = renderer.getScreenWidth() - kSideMargin - kTabWidth * 2;
        if (y >= bodyTop && y < bodyTop + kTabHeight && x >= tabsX && x < tabsX + kTabWidth * 2) {
          selectVariant((x - tabsX) / kTabWidth);
          return;
        }

        const int visibleRows = visibleRowCount(fontListTop);
        const int maxScroll = std::max(0, visiblePackageCount() - visibleRows);
        if (maxScroll > 0 && contains(scrollCaretBounds(renderer), x, y)) {
          if (scrollOffset_ >= maxScroll) {
            scrollOffset_ = std::max(0, scrollOffset_ - std::max(1, visibleRows - 1));
          } else {
            scrollOffset_ = std::min(maxScroll, scrollOffset_ + std::max(1, visibleRows - 1));
          }
          updateDisplay();
          return;
        }
        const int tapped = scrollOffset_ + (y - fontListTop) / kRowHeight;
        const int total = visiblePackageCount();
        if (x >= 0 && x < renderer.getScreenWidth() && y >= fontListTop &&
            y < fontListTop + visibleRows * kRowHeight && tapped >= 0 && tapped < total) {
          selectedIndex_ = tapped;
          const int packageIndex = packageIndexAt(tapped);
          const bool installed = packageIndex >= 0 &&
                                 FontPackageManager::isInstalled(packages_[static_cast<size_t>(packageIndex)]);
          const int deleteIconX = renderer.getScreenWidth() - kSideMargin - kActionIconSize;
          const ButtonBounds deleteBounds{deleteIconX, fontListTop + (tapped - scrollOffset_) * kRowHeight,
                                          kActionIconSize, kRowHeight};
          if (installed && contains(deleteBounds, x, y)) {
            removeSelected();
          } else {
            installSelected();
          }
        }
      } else if (state_ == State::Failed && contains(actionBounds(renderer), x, y)) {
        state_ = State::Loading;
        status_.clear();
        updateDisplay();
        loadPackages();
      } else if (state_ == State::Ready && packages_.empty()) {
        goBack_();
      }
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    goBack_();
    return;
  }
  if (state_ == State::Ready && !packages_.empty()) {
    const int total = visiblePackageCount();
    if (total == 0) return;
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      selectedIndex_ = (selectedIndex_ + 1) % total;
      selectedVisible_ = true;
      updateDisplay();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      selectedIndex_ = (selectedIndex_ + total - 1) % total;
      selectedVisible_ = true;
      updateDisplay();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      installSelected();
      return;
    }
  } else if (state_ == State::Failed && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    state_ = State::Loading;
    updateDisplay();
    loadPackages();
  }
}

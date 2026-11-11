#include "FontManagerActivity.h"
#include "SettingsListLayout.h"

#include <GfxRenderer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "activity/page/SubPage.h"
#include "activity/network/WifiSelectionActivity.h"
#include "activity/page/components/global/Button.h"
#include "activity/page/components/global/Paginator.h"
#include "activity/page/components/widget/BaseCarousel.h"
#include "images/Download.h"
#include "images/LibraryFilterRight.h"
#include "images/Trash.h"
#include "state/ReaderPreset.h"
#include "state/ReaderSetting.h"
#include "system/FontManager.h"
#include "system/FontPreviews.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/StringUtils.h"
#if FREEINK_DEVICE_X4PRO
#include "images/X4ProWifiTransferSetup.h"
#elif FREEINK_DEVICE_STICKY || FREEINK_DEVICE_MURPHY_M4
#include "images/MurphyStickyWifiTransferSetup.h"
#else
#include "images/WifiTransferSetup.h"
#endif

namespace {
// Rendering the font list can traverse preview glyphs, build several strings,
// and enter the renderer's text path. Four kilobytes is not enough on ESP32-S3
// and trips the stack canary before the download even starts.
constexpr uint32_t kDisplayTaskStack = 8192;
constexpr uint32_t kInstallTaskStack = 8192;
constexpr int kBottomMargin = 44;
constexpr int kSideMargin = 20;
constexpr int kActionIconSize = 40;
constexpr int kScrollCaretSize = 40;
constexpr int kCategoryFilterWidth = 220;
constexpr int kCategoryFilterRowHeight = UiLayout::LIST_ITEM_HEIGHT;
constexpr int kTransferImageWidth = 400;
constexpr int kTransferImageHeight = 200;
constexpr int kTransferImageGap = 15;
constexpr int kTransferCardPadding = 20;
constexpr int kTransferCardGap = 12;
constexpr int kTransferPanelButtonGap = 20;
constexpr int kTransferTextGap = 20;
// Only a swipe that starts at the physical bottom edge closes this page.  A
// normal upward swipe in the font list remains a list-page navigation gesture.
constexpr float kBottomEdgeSwipeStartNy = 0.95f;

uint8_t fontFamilySlotForLabel(const std::vector<std::string>& labels, const std::string& label) {
  const auto it = std::find(labels.begin(), labels.end(), label);
  return it == labels.end() ? 0 : static_cast<uint8_t>(std::distance(labels.begin(), it));
}

int pageBodyTop() { return FREEINK_DEVICE_X4PRO ? 80 : 70; }

#if FREEINK_DEVICE_X4PRO
const uint8_t* const kTransferImage = X4ProWifiTransferSetup;
#elif FREEINK_DEVICE_STICKY || FREEINK_DEVICE_MURPHY_M4
const uint8_t* const kTransferImage = MurphyStickyWifiTransferSetup;
#else
const uint8_t* const kTransferImage = WifiTransferSetup;
#endif

bool contains(const ButtonBounds& bounds, const int x, const int y) {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}

std::string displayFontName(const std::string& name) {
  std::string result;
  result.reserve(name.size() + 8);
  for (size_t i = 0; i < name.size(); ++i) {
    const unsigned char current = static_cast<unsigned char>(name[i]);
    if (current == '-' || current == '_') {
      if (!result.empty() && result.back() != ' ') result.push_back(' ');
      continue;
    }

    const unsigned char previous = i > 0 ? static_cast<unsigned char>(name[i - 1]) : 0;
    const unsigned char next = i + 1 < name.size() ? static_cast<unsigned char>(name[i + 1]) : 0;
    const bool startsWord = std::isupper(current) &&
                            (std::islower(previous) || std::isdigit(previous) ||
                             (std::isupper(previous) && std::islower(next)));
    const bool startsNumber = std::isdigit(current) && std::isalpha(previous);
    if ((startsWord || startsNumber) && !result.empty() && result.back() != ' ') result.push_back(' ');
    result.push_back(static_cast<char>(current));
  }
  return result;
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

ButtonBounds scrollCaretBounds(const GfxRenderer& renderer) {
  return {renderer.getScreenWidth() - kSideMargin - kScrollCaretSize, renderer.getScreenHeight() - kBottomMargin + 2,
          kScrollCaretSize, kScrollCaretSize};
}

void renderListDivider(const GfxRenderer& renderer, const int y) {
  constexpr int inset = SettingsListLayout::kDividerInset;
  const ButtonBounds caret = scrollCaretBounds(renderer);
  const int right = renderer.getScreenWidth() - inset;
  if (y >= caret.y && y < caret.y + caret.height) {
    constexpr int gap = 2;
    const int leftEnd = caret.x - gap;
    const int rightStart = caret.x + caret.width + gap;
    if (leftEnd > inset) {
      renderer.line.render(inset, y, leftEnd, y, true, LineRender::Style::Dotted);
    }
    if (rightStart < right) {
      renderer.line.render(rightStart, y, right, y, true, LineRender::Style::Dotted);
    }
    return;
  }
  renderer.line.render(inset, y, right, y, true, LineRender::Style::Dotted);
}

}

void FontManagerActivity::displayTaskTrampoline(void* param) {
  static_cast<FontManagerActivity*>(param)->displayTaskLoop();
}

void FontManagerActivity::installTaskTrampoline(void* param) {
  static_cast<FontManagerActivity*>(param)->installTaskLoop();
}

void FontManagerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  state_ = State::Ready;
  status_.clear();
  selectedIndex_ = 0;
  scrollOffset_ = 0;
  paginatorPage_ = 0;
  selectedVisible_ = false;
  categoryFilterOpen_ = false;
  categoryFilter_ = CategoryFilter::All;
  packages_.clear();
  installingPackageIndex_ = -1;
  progressDownloaded_ = 0;
  progressTotal_ = 0;
  updateRequired_ = false;
  shuttingDown_ = false;
  installingFont_ = false;
  fontCatalogNeedsRescan_ = false;
  fontFamilyLabelsBeforeInstall_.clear();
  globalFontFamilyBeforeInstall_.clear();
  systemFontFamilyBeforeInstall_.clear();
  lastProgressPercent_ = -1;
  lastProgressUpdateMs_ = 0;
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
  if (fontCatalogNeedsRescan_) {
    rescanFontCatalogPreservingSettings();
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
  // The lower paginator/caret area still leaves room for one additional row.
  return std::max(1, (renderer.getScreenHeight() - kBottomMargin - bodyTop) / kRowHeight + 1);
}

int FontManagerActivity::visiblePackageCount() const {
  int count = 0;
  for (const FontPackageManager::Package& package : packages_) {
    if (matchesCategory(package)) ++count;
  }
  return count;
}

int FontManagerActivity::packageIndexAt(const int visibleIndex) const {
  if (visibleIndex < 0) return -1;
  int index = 0;
  for (int packageIndex = 0; packageIndex < static_cast<int>(packages_.size()); ++packageIndex) {
    const FontPackageManager::Package& package = packages_[static_cast<size_t>(packageIndex)];
    if (!matchesCategory(package)) continue;
    if (index == visibleIndex) return packageIndex;
    ++index;
  }
  return -1;
}

int FontManagerActivity::categoryFilterCount() const { return 3; }

const char* FontManagerActivity::categoryFilterLabel() const {
  switch (categoryFilter_) {
    case CategoryFilter::SansSerif:
      return "Sans Serif";
    case CategoryFilter::Serif:
      return "Serif";
    case CategoryFilter::All:
    default:
      return "All";
  }
}

bool FontManagerActivity::matchesCategory(const FontPackageManager::Package& package) const {
  if (categoryFilter_ == CategoryFilter::All) return true;
  const auto category = categoryFilter_ == CategoryFilter::Serif ? FontPackageManager::Category::Serif
                                                                  : FontPackageManager::Category::SansSerif;
  return package.category == category;
}

ButtonBounds FontManagerActivity::categoryFilterBounds() const {
  return {renderer.getScreenWidth() - kSideMargin - kCategoryFilterWidth, pageBodyTop(), kCategoryFilterWidth,
          kFilterHeight};
}

void FontManagerActivity::categoryFilterDropdown() const {
  const int x = renderer.getScreenWidth() - kSideMargin - kCategoryFilterWidth;
  const int y = pageBodyTop() + kFilterHeight;
  const int height = categoryFilterCount() * kCategoryFilterRowHeight + 1;
  const char* labels[] = {"All", "Sans Serif", "Serif"};
  const int font = systemFontId();

  renderer.rectangle.fill(x, y, kCategoryFilterWidth, height, false);
  for (int index = 0; index < categoryFilterCount(); ++index) {
    const int rowY = y + index * kCategoryFilterRowHeight;
    const int textY = rowY + (kCategoryFilterRowHeight - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, x + 20, textY, labels[index], true, EpdFontFamily::REGULAR);
    if (index + 1 < categoryFilterCount()) {
      renderer.line.render(x + 10, rowY + kCategoryFilterRowHeight, x + kCategoryFilterWidth - 10,
                           rowY + kCategoryFilterRowHeight, true, LineRender::Style::Dotted);
    }
  }
  renderer.rectangle.render(x, y, kCategoryFilterWidth, height, true);
}

void FontManagerActivity::handleCategoryFilterTap(const int tapX, const int tapY) {
  const int x = renderer.getScreenWidth() - kSideMargin - kCategoryFilterWidth;
  const int y = pageBodyTop() + kFilterHeight;
  const int height = categoryFilterCount() * kCategoryFilterRowHeight + 1;
  if (tapX >= x && tapX < x + kCategoryFilterWidth && tapY >= y && tapY < y + height) {
    applyCategoryFilter((tapY - y) / kCategoryFilterRowHeight);
  } else {
    categoryFilterOpen_ = false;
    updateDisplay();
  }
}

void FontManagerActivity::applyCategoryFilter(const int index) {
  if (index < 0 || index >= categoryFilterCount()) return;
  categoryFilter_ = static_cast<CategoryFilter>(index);
  selectedIndex_ = 0;
  scrollOffset_ = 0;
  paginatorPage_ = 0;
  selectedVisible_ = false;
  categoryFilterOpen_ = false;
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
    if (fontFamilyLabelsBeforeInstall_.empty()) {
      fontFamilyLabelsBeforeInstall_ = FontManager::readerFontFamilyEnumLabels();
      globalFontFamilyBeforeInstall_ = FontManager::readerFontFamilyLabel(READER_SETTINGS.fontFamily);
      systemFontFamilyBeforeInstall_ = FontManager::readerFontFamilyLabel(SETTINGS.systemFontFamily);
    }
    launchWifiSelection();
    return;
  }

  if (fontFamilyLabelsBeforeInstall_.empty()) {
    fontFamilyLabelsBeforeInstall_ = FontManager::readerFontFamilyEnumLabels();
    globalFontFamilyBeforeInstall_ = FontManager::readerFontFamilyLabel(READER_SETTINGS.fontFamily);
    systemFontFamilyBeforeInstall_ = FontManager::readerFontFamilyLabel(SETTINGS.systemFontFamily);
  }
  state_ = State::Downloading;
  status_ = "Downloading and installing...";
  installingPackageIndex_ = packageIndex;
  installingFont_ = false;
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

  const uint8_t newFamily = static_cast<uint8_t>(std::distance(families.begin(), familyIt));
  const bool wasOutline = FontManager::isOutlineFontFamilySlot(READER_SETTINGS.fontFamily);
  const bool isOutline = FontManager::isOutlineFontFamilySlot(newFamily);
  if (isOutline && !wasOutline) {
    READER_SETTINGS.fontSize = static_cast<uint8_t>(
        FontManager::pointSizeForLegacyReaderSize(READER_SETTINGS.fontSize));
  } else if (!isOutline && wasOutline) {
    READER_SETTINGS.fontSize = FontManager::legacyReaderSizeForPointSize(READER_SETTINGS.fontSize);
  }
  READER_SETTINGS.fontFamily = newFamily;
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
    READER_SETTINGS.fontFamily = SystemSetting::MONTSERRAT;
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
      xTaskCreatePinnedToCoreWithCaps(&FontManagerActivity::displayTaskTrampoline, "FontDisplayTask", kDisplayTaskStack,
                                      this, 1, &displayTaskHandle_, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    state_ = State::Failed;
    status_ = "Could not start download.";
    updateDisplay();
    return;
  }
  INX_SERIAL.printf("[%lu] [FONT-UI] display task ready handle=%p\n", millis(), displayTaskHandle_);

  updateRequired_ = true;
  if (xTaskCreatePinnedToCoreWithCaps(&FontManagerActivity::installTaskTrampoline, "FontInstallTask", kInstallTaskStack,
                                      this, 1, &installTaskHandle_, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    installTaskHandle_ = nullptr;
    state_ = State::Failed;
    status_ = "Could not start download.";
    updateRequired_ = true;
  }
}

void FontManagerActivity::displayTaskLoop() {
  INX_SERIAL.printf("[%lu] [FONT-UI] display task started\n", millis());
  while (true) {
    if (updateRequired_) {
      updateRequired_ = false;
      if (renderingMutex_ && xSemaphoreTake(renderingMutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        INX_SERIAL.printf("[%lu] [FONT-UI] render state=%d downloaded=%u total=%u\n", millis(),
                          static_cast<int>(state_), static_cast<unsigned>(progressDownloaded_),
                          static_cast<unsigned>(progressTotal_));
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
  installingFont_ = false;
  if (packageIndex >= 0 && packageIndex < static_cast<int>(packages_.size())) {
    installed = FontPackageManager::install(
        packages_[static_cast<size_t>(packageIndex)], error,
        [this](const size_t downloaded, const size_t total) {
          progressDownloaded_ = downloaded;
          progressTotal_ = total;
          if (total > 0 && downloaded >= total) installingFont_ = true;
          const int percent = total > 0 ? static_cast<int>((downloaded * 100) / total) : 0;
          const unsigned long now = millis();
          if (percent != lastProgressPercent_ || now - lastProgressUpdateMs_ >= 750) {
            lastProgressPercent_ = percent;
            lastProgressUpdateMs_ = now;
            updateRequired_ = true;
          }
        },
        false);
    if (installed) {
      fontCatalogNeedsRescan_ = true;
      if (renderingMutex_ && xSemaphoreTake(renderingMutex_, portMAX_DELAY) == pdTRUE) {
        rescanFontCatalogPreservingSettings();
        xSemaphoreGive(renderingMutex_);
      }
    }
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
  INX_SERIAL.printf("[%lu] [FONT-UI] install returned installed=%d state=%d\n", millis(), installed,
                    static_cast<int>(state_));
  installTaskHandle_ = nullptr;
  vTaskDeleteWithCaps(nullptr);
}

void FontManagerActivity::rescanFontCatalogPreservingSettings() {
  if (!fontCatalogNeedsRescan_) return;

  const std::vector<std::string> oldLabels = fontFamilyLabelsBeforeInstall_.empty()
                                                 ? FontManager::readerFontFamilyEnumLabels()
                                                 : fontFamilyLabelsBeforeInstall_;
  const std::string globalFamily = globalFontFamilyBeforeInstall_.empty()
                                       ? FontManager::readerFontFamilyLabel(READER_SETTINGS.fontFamily)
                                       : globalFontFamilyBeforeInstall_;
  const std::string systemFamily = systemFontFamilyBeforeInstall_.empty()
                                       ? FontManager::readerFontFamilyLabel(SETTINGS.systemFontFamily)
                                       : systemFontFamilyBeforeInstall_;
  if (!FontManager::scanSDFonts("/fonts", true)) return;

  const std::vector<std::string> newLabels = FontManager::readerFontFamilyEnumLabels();
  READER_SETTINGS.fontFamily = fontFamilySlotForLabel(newLabels, globalFamily);
  READER_SETTINGS.saveToFile();
  SETTINGS.systemFontFamily = fontFamilySlotForLabel(newLabels, systemFamily);
  SETTINGS.saveToFile();
  READER_PRESETS.remapFontFamilySlots(oldLabels, newLabels);
  fontCatalogNeedsRescan_ = false;
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
                                             [this](const bool connected) { onWifiSelectionComplete(connected); },
                                             "Download", "Fonts"));
}

void FontManagerActivity::onWifiSelectionComplete(const bool connected) {
  INX_SERIAL.printf("[%lu] [FONT-WIFI] complete connected=%d status=%d ip=%s\n", millis(), connected,
                    static_cast<int>(WiFi.status()), WiFi.localIP().toString().c_str());
  exitActivity();
  INX_SERIAL.printf("[%lu] [FONT-WIFI] subactivity exited\n", millis());
  if (!connected || WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    state_ = State::Failed;
    status_ = "Wi-Fi connection failed. Tap Retry.";
    render();
    return;
  }
  INX_SERIAL.printf("[%lu] [FONT-WIFI] starting font install\n", millis());
  renderer.syncWriteBufferFromActive();
  installSelected();
}


void FontManagerActivity::render() {
  // WifiSelectionActivity leaves the X4 Pro renderer after an async refresh.
  // Re-seed the writable framebuffer from the frame actually on the panel
  // before replacing it with the Font Manager screen.
  renderer.syncWriteBufferFromActive();
  renderer.clearScreen();
  const int bodyTop = SubPage::header(renderer, "Font");
  const int fontListTop = listTop(bodyTop);
  const int font = systemFontId();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  if (state_ == State::Downloading) {
    const int packageIndex = installingPackageIndex_;
    const std::string packageName = packageIndex >= 0 && packageIndex < static_cast<int>(packages_.size())
                                        ? displayFontName(packages_[static_cast<size_t>(packageIndex)].name)
                                        : "Font";
    const size_t downloaded = progressDownloaded_;
    const size_t total = progressTotal_;
    const int percent = total > 0 ? std::max(0, std::min(100, static_cast<int>((downloaded * 100) / total))) : 0;
    const int imageX = (screenW - kTransferImageWidth) / 2;
    const int imageY = bodyTop;
    renderer.bitmap.icon(kTransferImage, imageX, imageY, kTransferImageWidth, kTransferImageHeight);
    const int panelY = imageY + kTransferImageHeight + kTransferImageGap;
    const int panelBottom = std::max(panelY + 1, screenH - kTransferPanelButtonGap);
    for (int py = panelY; py < panelBottom; py += 2) {
      const int offset = ((py - panelY) & 2) != 0 ? 0 : 1;
      for (int px = offset; px < screenW; px += 2) renderer.drawPixel(px, py, true);
    }
    const int cardX = kTransferCardPadding;
    const int cardY = panelY + 10;
    const int cardWidth = screenW - 2 * kTransferCardPadding;
    const int lineHeight = renderer.text.getLineHeight(font);
    const int headerY = cardY + kTransferCardPadding;
    const int nameY = headerY + lineHeight + kTransferTextGap;
    const int firstRowY = nameY + lineHeight + kTransferTextGap;
    const int barY = firstRowY + lineHeight + 2;
    const int secondRowY = barY + 14;
    const int cardHeight = std::min(panelBottom - cardY, secondRowY - cardY + lineHeight + kTransferCardPadding);
    const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
    BaseCarousel::renderShadow(renderer, cardX + 4, cardY + 4, cardWidth, cardHeight,
                               HomeTheme::CarouselShadowStyle::Gray);
    renderer.rectangle.fill(cardX, cardY, cardWidth, cardHeight, false, rounded);
    renderer.rectangle.render(cardX, cardY, cardWidth, cardHeight, true, rounded);
    const int contentX = cardX + kTransferCardPadding;
    renderer.text.render(font, contentX, headerY, "Font download", true, EpdFontFamily::BOLD);
    const std::string displayName = renderer.text.truncate(font, packageName.c_str(), cardWidth - 2 * kTransferCardPadding);
    renderer.text.render(font, contentX, nameY, displayName.c_str(), true);
    renderer.text.render(font, contentX, firstRowY, "Download", true);
    char percentText[8];
    std::snprintf(percentText, sizeof(percentText), "%d%%", percent);
    const int percentWidth = renderer.text.getWidth(font, percentText);
    renderer.text.render(font, cardX + cardWidth - kTransferCardPadding - percentWidth, firstRowY, percentText, true);
    drawProgressBar(renderer, contentX, barY, cardWidth - 2 * kTransferCardPadding, 6, percent);
    renderer.text.render(font, cardX + cardWidth - kTransferCardPadding -
                                  renderer.text.getWidth(font, installingFont_ ? "Installing" : "Waiting"),
                         secondRowY, installingFont_ ? "Installing" : "Waiting", true);
    mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
    renderer.displayBuffer();
    return;
  }

  if (!packages_.empty()) {
    const int total = visiblePackageCount();
    const int visibleRows = visibleRowCount(fontListTop);
    const int maxScroll = std::max(0, total - visibleRows);
    const int pageCount = std::max(1, (total + visibleRows - 1) / visibleRows);
    const int pageStep = std::max(1, visibleRows - 1);
    paginatorPage_ = std::max(0, std::min(paginatorPage_, pageCount - 1));
    scrollOffset_ = std::max(0, std::min(scrollOffset_, maxScroll));
    if (selectedVisible_) {
      if (selectedIndex_ < scrollOffset_) scrollOffset_ = selectedIndex_;
      if (selectedIndex_ >= scrollOffset_ + visibleRows) scrollOffset_ = selectedIndex_ - visibleRows + 1;
      paginatorPage_ = std::max(0, std::min(pageCount - 1, (scrollOffset_ + pageStep / 2) / pageStep));
    }

    Button::render(renderer, categoryFilterBounds(), categoryFilterLabel(), categoryFilterOpen_, font);

    const int end = std::min(total, scrollOffset_ + visibleRows);
    for (int index = scrollOffset_; index < end; ++index) {
      const int packageIndex = packageIndexAt(index);
      if (packageIndex < 0) continue;
      const int y = fontListTop + (index - scrollOffset_) * kRowHeight;
      const bool selected = selectedVisible_ && index == selectedIndex_;
      if (selected) renderer.rectangle.fill(0, y, screenW, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
      const int deleteIconX = screenW - kSideMargin - kActionIconSize;
      const bool downloadingCurrent = state_ == State::Downloading && packageIndex == installingPackageIndex_;
      const size_t downloaded = progressDownloaded_;
      const size_t totalBytes = progressTotal_;
      const int percent = totalBytes > 0
                              ? std::max(0, std::min(100, static_cast<int>((downloaded * 100) / totalBytes)))
                              : 0;
      const std::string percentText = std::to_string(percent) + "%";
      const int percentWidth = renderer.text.getWidth(font, percentText.c_str());
      const int maxNameWidth = downloadingCurrent
                                   ? screenW - (kSideMargin * 2) - percentWidth - 20
                                   : screenW - (kSideMargin * 2) - kActionIconSize - 20;
      const std::string displayName = displayFontName(packages_[static_cast<size_t>(packageIndex)].name);
      // FontPackageManager rescans and unloads SD streaming fonts when the
      // install finishes. Keep the download UI on the built-in system font so
      // it cannot render a preview font while that catalog is being replaced.
      const int previewFont = state_ == State::Downloading
                                  ? -1
                                  : FontPreviews::fontIdForFamily(packages_[static_cast<size_t>(packageIndex)].installFamily);
      const int nameFont = previewFont >= 0 ? previewFont : font;
      const int textY = y + (kRowHeight - renderer.text.getLineHeight(nameFont)) / 2;
      const std::string packageName = renderer.text.truncate(nameFont, displayName.c_str(), maxNameWidth,
                                                              EpdFontFamily::REGULAR);
      const bool installed = FontPackageManager::isInstalled(packages_[static_cast<size_t>(packageIndex)]);
      if (installed && !selected) {
        renderer.text.renderGray(nameFont, kSideMargin, textY, packageName.c_str(), true, EpdFontFamily::REGULAR);
      } else {
        renderer.text.render(nameFont, kSideMargin, textY, packageName.c_str(), !selected, EpdFontFamily::REGULAR);
      }
      const int iconY = y + (kRowHeight - kActionIconSize) / 2;
      if (downloadingCurrent) {
        renderer.text.render(font, screenW - kSideMargin - percentWidth,
                             y + (kRowHeight - renderer.text.getLineHeight(font)) / 2, percentText.c_str(),
                             !selected, EpdFontFamily::REGULAR);
      } else if (installed) {
        renderer.bitmap.icon(Trash, deleteIconX, iconY, kActionIconSize, kActionIconSize,
                             BitmapRender::Orientation::None, selected);
      } else {
        renderer.bitmap.icon(Download, deleteIconX, iconY, kActionIconSize, kActionIconSize,
                             BitmapRender::Orientation::None, selected);
      }
      if (index + 1 < end) {
        renderListDivider(renderer, y + kRowHeight - 1);
      }
    }
    Paginator::render(renderer, paginatorPage_, pageCount, screenW - 10, fontListTop,
                      visibleRows * kRowHeight);
    const ButtonBounds caretBounds = scrollCaretBounds(renderer);
    const auto caretOrientation = scrollOffset_ >= maxScroll ? BitmapRender::Orientation::Rotate270CW
                                                               : BitmapRender::Orientation::Rotate90CW;
    renderer.bitmap.iconScaled(LibraryFilterRight, caretBounds.x, caretBounds.y, 30, 30, kScrollCaretSize,
                               kScrollCaretSize, caretOrientation);
    mappedInput.mapLabels("\xC2\xAB Back", state_ == State::Downloading ? "" : "Download",
                          state_ == State::Downloading ? "" : "Up", state_ == State::Downloading ? "" : "Down");
    if (categoryFilterOpen_ && state_ != State::Downloading) categoryFilterDropdown();
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

  if (state_ != State::Downloading && mappedInput.hasTouch()) {
    float swipeStartNx = 0.0f;
    float swipeStartNy = 0.0f;
    if (mappedInput.wasTouchSwipeUpInScreen(renderer, swipeStartNx, swipeStartNy) &&
        swipeStartNy >= kBottomEdgeSwipeStartNy) {
      INX_SERIAL.printf("[%lu] [FONT-UI] bottom-edge swipe exit start=(%.3f,%.3f) state=%d\n", millis(),
                        swipeStartNx, swipeStartNy, static_cast<int>(state_));
      goBack_();
      return;
    }
  }

  if (state_ == State::Ready && !packages_.empty() && mappedInput.hasTouch()) {
    const int bodyTop = pageBodyTop();
    const int fontListTop = listTop(bodyTop);
    const bool swipeUp = mappedInput.wasTouchSwipeUpForRenderer(renderer);
    const bool swipeDown = mappedInput.wasTouchSwipeDownForRenderer(renderer);
    if (swipeUp || swipeDown) {
      const int visibleRows = visibleRowCount(fontListTop);
      const int maxScroll = std::max(0, visiblePackageCount() - visibleRows);
      const int pageCount = std::max(1, (visiblePackageCount() + visibleRows - 1) / visibleRows);
      const int pageStep = std::max(1, visibleRows - 1);
      // A touch scroll is navigation, not a new row selection. Do not let a
      // previously button-selected row pull the list back onto its old page.
      selectedVisible_ = false;
      if (swipeUp) {
        paginatorPage_ = std::min(pageCount - 1, paginatorPage_ + 1);
      } else {
        paginatorPage_ = std::max(0, paginatorPage_ - 1);
      }
      scrollOffset_ = std::min(maxScroll, paginatorPage_ * pageStep);
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
        if (categoryFilterOpen_) {
          handleCategoryFilterTap(x, y);
          return;
        }
        if (contains(categoryFilterBounds(), x, y)) {
          categoryFilterOpen_ = true;
          updateDisplay();
          return;
        }
        const int visibleRows = visibleRowCount(fontListTop);
        const int maxScroll = std::max(0, visiblePackageCount() - visibleRows);
        if (maxScroll > 0 && contains(scrollCaretBounds(renderer), x, y)) {
          const int pageCount = std::max(1, (visiblePackageCount() + visibleRows - 1) / visibleRows);
          const int pageStep = std::max(1, visibleRows - 1);
          selectedVisible_ = false;
          if (scrollOffset_ >= maxScroll) {
            paginatorPage_ = std::max(0, paginatorPage_ - 1);
          } else {
            paginatorPage_ = std::min(pageCount - 1, paginatorPage_ + 1);
          }
          scrollOffset_ = std::min(maxScroll, paginatorPage_ * pageStep);
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
    loadPackages();
  }
}

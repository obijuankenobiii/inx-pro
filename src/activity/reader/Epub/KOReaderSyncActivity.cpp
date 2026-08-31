/**
 * @file KOReaderSyncActivity.cpp
 * @brief Definitions for KOReaderSyncActivity.
 */

#include "KOReaderSyncActivity.h"

#include <GfxRenderer.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include "KOReaderCredentialStore.h"
#include "KOReaderDocumentId.h"
#include "activity/network/WifiSelectionActivity.h"
#include "activity/page/components/global/Button.h"
#include "activity/page/SubPage.h"
#include "images/Download.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/TimeZoneAutoDetect.h"

namespace {

ButtonBounds startButtonBounds(const GfxRenderer& renderer, const int font) {
  const int width = Button::width(renderer, "Start Sync", font);
  return {(renderer.getScreenWidth() - width) / 2, renderer.getScreenHeight() - Button::height - 30, width,
          Button::height};
}

int syncContentTop() {
  constexpr int headerTop = FREEINK_DEVICE_X4PRO ? 20 : 10;
  return headerTop + 40 + 20;
}

struct SyncActionButtons {
  ButtonBounds upload;
  ButtonBounds download;
};

constexpr int kSyncActionIconSize = 40;
constexpr int kSyncActionGap = 10;
constexpr int kSyncActionRightMargin = 20;
constexpr int kSyncActionBottomMargin = 20;

int syncActionButtonWidth(const GfxRenderer& renderer, const char* label, const int font) {
  return renderer.text.getWidth(font, label) + kSyncActionIconSize + kSyncActionGap + Button::horizontalPadding * 2;
}

SyncActionButtons syncActionButtons(const GfxRenderer& renderer, const int font) {
  const int uploadWidth = syncActionButtonWidth(renderer, "Upload", font);
  const int downloadWidth = syncActionButtonWidth(renderer, "Download", font);
  const int y = renderer.getScreenHeight() - kSyncActionBottomMargin - Button::height;
  const int downloadX = renderer.getScreenWidth() - kSyncActionRightMargin - downloadWidth;
  const int uploadX = downloadX - kSyncActionGap - uploadWidth;
  return {{uploadX, y, uploadWidth, Button::height}, {downloadX, y, downloadWidth, Button::height}};
}

ButtonBounds singleUploadButton(const GfxRenderer& renderer, const int font) {
  const int width = syncActionButtonWidth(renderer, "Upload", font);
  return {renderer.getScreenWidth() - kSyncActionRightMargin - width,
          renderer.getScreenHeight() - kSyncActionBottomMargin - Button::height, width, Button::height};
}

void renderSyncActionButton(const GfxRenderer& renderer, const ButtonBounds& bounds, const char* label,
                            const bool fill, const BitmapRender::Orientation iconOrientation, const int font) {
  // Use the shared button component for the fill and border, then place the icon
  // and label as one compound action without changing the global button layout.
  Button::render(renderer, bounds, "", fill, font);
  const int labelWidth = renderer.text.getWidth(font, label);
  const int contentWidth = kSyncActionIconSize + kSyncActionGap + labelWidth;
  const int contentX = bounds.x + (bounds.width - contentWidth) / 2;
  const int iconY = bounds.y + (bounds.height - kSyncActionIconSize) / 2;
  const int textY = bounds.y + (bounds.height - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, contentX, textY, label, !fill, EpdFontFamily::REGULAR);
  renderer.bitmap.icon(Download, contentX + labelWidth + kSyncActionGap, iconY, kSyncActionIconSize,
                       kSyncActionIconSize, iconOrientation, fill);
}

bool touchPointInBounds(MappedInputManager& input, const GfxRenderer& renderer, const ButtonBounds& bounds, int& x,
                        int& y) {
  if (!input.hasTouch()) return false;

  float tapX = 0.0f;
  float tapY = 0.0f;
  if (!input.wasTouchTapInScreen(renderer, tapX, tapY)) return false;

  x = static_cast<int>(tapX * renderer.getScreenWidth());
  y = static_cast<int>(tapY * renderer.getScreenHeight());
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}

void wifiOff() {
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}

void syncTimeWithNTP() {
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  int retry = 0;
  const int maxRetries = 50;
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && retry < maxRetries) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
    retry++;
  }

  if (retry < maxRetries) {
    INX_SERIAL.printf("[%lu] [KOSync] NTP time synced\n", millis());
    autoDetectTimeZone();
  } else {
    INX_SERIAL.printf("[%lu] [KOSync] NTP sync timeout, using fallback\n", millis());
  }
}
}  // namespace

void KOReaderSyncActivity::taskTrampoline(void* param) {
  auto* self = static_cast<KOReaderSyncActivity*>(param);
  self->displayTaskLoop();
}

void KOReaderSyncActivity::onWifiSelectionComplete(const bool success) {
  exitActivity();

  if (!success) {
    INX_SERIAL.printf("[%lu] [KOSync] WiFi connection failed, exiting\n", millis());
    onCancel();
    return;
  }

  INX_SERIAL.printf("[%lu] [KOSync] WiFi connected, starting sync\n", millis());

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = SYNCING;
  statusMessage = "Syncing time...";
  xSemaphoreGive(renderingMutex);
  updateRequired = true;

  syncTimeWithNTP();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  statusMessage = "Calculating document hash...";
  xSemaphoreGive(renderingMutex);
  updateRequired = true;

  performSync();
}

void KOReaderSyncActivity::performSync() {
  if (KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME) {
    documentHash = KOReaderDocumentId::calculateFromFilename(epubPath);
  } else {
    documentHash = KOReaderDocumentId::calculate(epubPath);
  }
  if (documentHash.empty()) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = SYNC_FAILED;
    statusMessage = "Failed to calculate document hash";
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  INX_SERIAL.printf("[%lu] [KOSync] Document hash: %s\n", millis(), documentHash.c_str());

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  statusMessage = "Fetching remote progress...";
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
  vTaskDelay(10 / portTICK_PERIOD_MS);

  const auto result = KOReaderSyncClient::getProgress(documentHash, remoteProgress);

  if (result == KOReaderSyncClient::NOT_FOUND) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = NO_REMOTE_PROGRESS;
    hasRemoteProgress = false;
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  if (result != KOReaderSyncClient::OK) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = SYNC_FAILED;
    statusMessage = KOReaderSyncClient::errorString(result);
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  hasRemoteProgress = true;
  KOReaderPosition koPos = {remoteProgress.progress, remoteProgress.percentage};
  remotePosition = ProgressMapper::toCrossPoint(epub, koPos, currentSpineIndex, totalPagesInSpine);

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = SHOWING_RESULT;

  if (localProgress.percentage > remoteProgress.percentage) {
    selectedOption = 1;
  } else {
    selectedOption = 0;
  }
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
}

void KOReaderSyncActivity::performUpload() {
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = UPLOADING;
  statusMessage = "Uploading progress...";
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
  vTaskDelay(10 / portTICK_PERIOD_MS);

  KOReaderProgress progress;
  progress.document = documentHash;
  progress.progress = localProgress.xpath;
  progress.percentage = localProgress.percentage;

  const auto result = KOReaderSyncClient::updateProgress(progress);

  if (result != KOReaderSyncClient::OK) {
    wifiOff();
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = SYNC_FAILED;
    statusMessage = KOReaderSyncClient::errorString(result);
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  wifiOff();
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = UPLOAD_COMPLETE;
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
}

void KOReaderSyncActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  xTaskCreate(&KOReaderSyncActivity::taskTrampoline, "KOSyncTask", 4096, this, 1, &displayTaskHandle);

  if (!KOREADER_STORE.hasCredentials()) {
    state = NO_CREDENTIALS;
    updateRequired = true;
    return;
  }

  state = IDLE;
  updateRequired = true;
}

void KOReaderSyncActivity::startSync() {
  if (!KOREADER_STORE.hasCredentials() || state != IDLE) {
    return;
  }

  INX_SERIAL.printf("[%lu] [KOSync] Turning on WiFi...\n", millis());
  WiFi.mode(WIFI_STA);

  if (WiFi.status() == WL_CONNECTED) {
    INX_SERIAL.printf("[%lu] [KOSync] Already connected to WiFi\n", millis());
    state = SYNCING;
    statusMessage = "Syncing time...";
    updateRequired = true;

    xTaskCreate(
        [](void* param) {
          auto* self = static_cast<KOReaderSyncActivity*>(param);

          syncTimeWithNTP();
          xSemaphoreTake(self->renderingMutex, portMAX_DELAY);
          self->statusMessage = "Calculating document hash...";
          xSemaphoreGive(self->renderingMutex);
          self->updateRequired = true;
          self->performSync();
          vTaskDelete(nullptr);
        },
        "SyncTask", 4096, this, 1, nullptr);
    return;
  }

  INX_SERIAL.printf("[%lu] [KOSync] Launching WifiSelectionActivity...\n", millis());
  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
}

void KOReaderSyncActivity::onExit() {
  ActivityWithSubactivity::onExit();

  wifiOff();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void KOReaderSyncActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void KOReaderSyncActivity::render() {
  if (subActivity) {
    return;
  }

  renderer.clearScreen();
  const int contentTop = SubPage::header(renderer, "KOReader Sync");
  const int font = systemFontId();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentCenterY = contentTop + (pageHeight - contentTop) / 2;

  if (state == NO_CREDENTIALS) {
    renderer.text.centered(font, contentCenterY - 20, "No credentials configured", true,
                           EpdFontFamily::BOLD);
    renderer.text.centered(font, contentCenterY + 20, "Set up KOReader account in Settings");

    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    renderer.displayBuffer();
    return;
  }

  if (state == IDLE) {
    renderer.text.centered(font, contentCenterY - 30, "Ready to sync this book", true,
                           EpdFontFamily::BOLD);
    Button::render(renderer, startButtonBounds(renderer, font), "Start Sync", true, font);
    renderer.displayBuffer();
    return;
  }

  if (state == SYNCING || state == UPLOADING) {
    renderer.text.centered(font, contentCenterY, statusMessage.c_str(), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == SHOWING_RESULT) {
    const int resultTop = contentTop + 32;
    const int remoteTocIndex = epub->getTocIndexForSpineIndex(remotePosition.spineIndex);
    const std::string remoteChapter = (remoteTocIndex >= 0)
                                          ? epub->getTocItem(remoteTocIndex).title
                                          : ("Section " + std::to_string(remotePosition.spineIndex + 1));
    const std::string localChapter =
        !localChapterName.empty() ? localChapterName : ("Section " + std::to_string(currentSpineIndex + 1));

    const int left = 20;
    const int remoteY = resultTop + 40;
    renderer.text.render(font, left, remoteY, "Remote", true, EpdFontFamily::BOLD);
    renderer.text.render(font, left, remoteY + 35, remoteChapter.c_str());
    char remotePageStr[64];
    snprintf(remotePageStr, sizeof(remotePageStr), "Page %d, %.2f%% overall", remotePosition.pageNumber + 1,
             remoteProgress.percentage * 100);
    renderer.text.render(font, left, remoteY + 65, remotePageStr);

    if (!remoteProgress.device.empty()) {
      char deviceStr[64];
      snprintf(deviceStr, sizeof(deviceStr), "From: %s", remoteProgress.device.c_str());
      renderer.text.render(font, left, remoteY + 95, deviceStr);
    }

    const int localY = remoteY + (remoteProgress.device.empty() ? 135 : 155);
    renderer.text.render(font, left, localY, "Local", true, EpdFontFamily::BOLD);
    renderer.text.render(font, left, localY + 35, localChapter.c_str());
    char localPageStr[64];
    snprintf(localPageStr, sizeof(localPageStr), "Page %d/%d, %.2f%% overall", currentPage + 1, totalPagesInSpine,
             localProgress.percentage * 100);
    renderer.text.render(font, left, localY + 65, localPageStr);

    const SyncActionButtons actions = syncActionButtons(renderer, font);
    renderSyncActionButton(renderer, actions.upload, "Upload", selectedOption == 1,
                           BitmapRender::Orientation::Rotate180, font);
    renderSyncActionButton(renderer, actions.download, "Download", selectedOption == 0,
                           BitmapRender::Orientation::None, font);

    const auto labels = mappedInput.mapLabels("Back", "Select", "Dir Up", "Dir Down");
    renderer.displayBuffer();
    return;
  }

  if (state == NO_REMOTE_PROGRESS) {
    renderer.text.centered(font, contentCenterY - 20, "No remote progress found", true,
                           EpdFontFamily::BOLD);
    renderer.text.centered(font, contentCenterY + 20, "Upload current position?");
    renderSyncActionButton(renderer, singleUploadButton(renderer, font), "Upload", true,
                           BitmapRender::Orientation::Rotate180, font);

    const auto labels = mappedInput.mapLabels("Cancel", "Upload", "", "");
    renderer.displayBuffer();
    return;
  }

  if (state == UPLOAD_COMPLETE) {
    renderer.text.centered(font, contentCenterY, "Progress uploaded!", true, EpdFontFamily::BOLD);

    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    renderer.displayBuffer();
    return;
  }

  if (state == SYNC_FAILED) {
    renderer.text.centered(font, contentCenterY - 20, "Sync failed", true, EpdFontFamily::BOLD);
    renderer.text.centered(font, contentCenterY + 20, statusMessage.c_str());

    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    renderer.displayBuffer();
    return;
  }
}

void KOReaderSyncActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (SubPage::closeInput(renderer, mappedInput, onCancel)) {
    return;
  }

  if (state == IDLE) {
    bool start = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
    int tapX = 0;
    int tapY = 0;
    if (!start) start = touchPointInBounds(mappedInput, renderer, startButtonBounds(renderer, systemFontId()), tapX, tapY);
    if (start) {
      startSync();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onCancel();
    }
    return;
  }

  if (state == NO_CREDENTIALS || state == SYNC_FAILED || state == UPLOAD_COMPLETE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onCancel();
    }
    return;
  }

  if (state == SHOWING_RESULT) {
    int tapX = 0;
    int tapY = 0;
    const SyncActionButtons actions = syncActionButtons(renderer, systemFontId());
    if (touchPointInBounds(mappedInput, renderer, actions.download, tapX, tapY)) {
      selectedOption = 0;
      onSyncComplete(remotePosition.spineIndex, remotePosition.pageNumber);
      return;
    }
    if (touchPointInBounds(mappedInput, renderer, actions.upload, tapX, tapY)) {
      selectedOption = 1;
      performUpload();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      selectedOption = (selectedOption + 1) % 2;
      updateRequired = true;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedOption == 0) {
        onSyncComplete(remotePosition.spineIndex, remotePosition.pageNumber);
      } else {
        performUpload();
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onCancel();
    }
    return;
  }

  if (state == NO_REMOTE_PROGRESS) {
    int tapX = 0;
    int tapY = 0;
    const ButtonBounds uploadBounds = singleUploadButton(renderer, systemFontId());
    const bool upload = touchPointInBounds(mappedInput, renderer, uploadBounds, tapX, tapY);
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (documentHash.empty()) {
        if (KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME) {
          documentHash = KOReaderDocumentId::calculateFromFilename(epubPath);
        } else {
          documentHash = KOReaderDocumentId::calculate(epubPath);
        }
      }
      performUpload();
    } else if (upload) {
      if (documentHash.empty()) {
        if (KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME) {
          documentHash = KOReaderDocumentId::calculateFromFilename(epubPath);
        } else {
          documentHash = KOReaderDocumentId::calculate(epubPath);
        }
      }
      performUpload();
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onCancel();
    }
    return;
  }
}

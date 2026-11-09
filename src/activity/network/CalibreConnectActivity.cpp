/**
 * @file CalibreConnectActivity.cpp
 * @brief Definitions for CalibreConnectActivity.
 */

#include "CalibreConnectActivity.h"

#include <algorithm>
#include <sstream>

#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Button.h"
#include "activity/page/components/widget/BaseCarousel.h"
#include "WifiSelectionActivity.h"
#include "images/Check.h"
#if FREEINK_DEVICE_X4PRO
#include "images/X4ProWifiTransferSetup.h"
#elif FREEINK_DEVICE_STICKY || FREEINK_DEVICE_MURPHY_M4
#include "images/MurphyStickyWifiTransferSetup.h"
#else
#include "images/WifiTransferSetup.h"
#endif
#include "state/SystemSetting.h"
#include "state/HomeTheme.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiLayout.h"

namespace {

/**
 * @brief mDNS hostname for device discovery on local network
 */
constexpr const char* HOSTNAME = "inx";
constexpr int IMAGE_WIDTH = 400;
constexpr int IMAGE_HEIGHT = 200;
constexpr int IMAGE_GAP = 15;
constexpr int CARD_PADDING = 20;
constexpr int CARD_GAP = 12;
constexpr int ACTION_MARGIN = 20;
constexpr int PANEL_BUTTON_GAP = 20;
constexpr int SUCCESS_ICON_SIZE = 40;

#if FREEINK_DEVICE_X4PRO
const uint8_t* const kWifiTransferSetup = X4ProWifiTransferSetup;
#elif FREEINK_DEVICE_STICKY || FREEINK_DEVICE_MURPHY_M4
const uint8_t* const kWifiTransferSetup = MurphyStickyWifiTransferSetup;
#else
const uint8_t* const kWifiTransferSetup = WifiTransferSetup;
#endif

ButtonBounds disconnectBounds(const GfxRenderer& renderer) {
  const int width = Button::width(renderer, "Disconnect", systemFontId());
  return {renderer.getScreenWidth() - ACTION_MARGIN - width,
          renderer.getScreenHeight() - ACTION_MARGIN - Button::height, width, Button::height};
}

bool contains(const ButtonBounds& bounds, const int x, const int y) {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}

struct WrappedInstructionLine {
  std::string text;
  int indent = 0;
};

std::vector<WrappedInstructionLine> wrapInstruction(const GfxRenderer& renderer, const int font,
                                                    const std::string& text, const int maxWidth,
                                                    const int continuationIndent) {
  std::vector<WrappedInstructionLine> lines;
  std::istringstream words(text);
  std::string word;
  std::string line;
  int indent = 0;
  while (words >> word) {
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (!line.empty() && renderer.text.getWidth(font, candidate.c_str()) > maxWidth - indent) {
      lines.push_back({line, indent});
      line = word;
      indent = continuationIndent;
    } else {
      line = candidate;
    }
  }
  if (!line.empty()) lines.push_back({line, indent});
  return lines;
}
}

/**
 * @brief Destructor - stops web server and cleans up resources
 */
CalibreConnectActivity::~CalibreConnectActivity() { stopWebServer(); }

/**
 * @brief Static trampoline function for FreeRTOS task creation
 * @param param Pointer to CalibreConnectActivity instance
 */
void CalibreConnectActivity::taskTrampoline(void* param) {
  auto* self = static_cast<CalibreConnectActivity*>(param);
  self->displayTaskLoop();
}

/**
 * @brief Initializes the activity when entering, sets up WiFi or web server
 */
void CalibreConnectActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  updateRequired = true;
  state = CalibreConnectState::WIFI_SELECTION;
  connectedIP.clear();
  connectedSSID.clear();
  lastProgressReceived = 0;
  lastProgressTotal = 0;
  currentUploadName.clear();
  lastCompleteName.clear();
  lastCompleteAt = 0;
  lastProcessedCompleteAt = 0;
  receivedFiles.clear();

  const bool alreadyConnected = WiFi.status() == WL_CONNECTED;
  if (!alreadyConnected) {
    enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                               [this](const bool connected) { onWifiSelectionComplete(connected); },
                                               "File Transfer", "Calibre"));
  } else {
    connectedIP = WiFi.localIP().toString().c_str();
    connectedSSID = WiFi.SSID().c_str();
  }

  xTaskCreateWithCaps(&CalibreConnectActivity::taskTrampoline, "CalibreConnectTask", 8192, this, 1,
                      &displayTaskHandle, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (alreadyConnected) {
    startWebServer();
  }

  renderer.syncWriteBufferFromActive();
}

/**
 * @brief Cleans up resources when exiting the activity
 */
void CalibreConnectActivity::onExit() {
  if (renderingMutex) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    updateRequired = false;
    if (displayTaskHandle) {
      vTaskDeleteWithCaps(displayTaskHandle);
      displayTaskHandle = nullptr;
    }
    xSemaphoreGive(renderingMutex);
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  } else if (displayTaskHandle) {
    vTaskDeleteWithCaps(displayTaskHandle);
    displayTaskHandle = nullptr;
  }

  stopWebServer();
  MDNS.end();

  delay(50);
  WiFi.disconnect(false);
  delay(30);
  WiFi.mode(WIFI_OFF);

  ActivityWithSubactivity::onExit();
}

/**
 * @brief Callback handler for WiFi selection completion
 * @param connected True if WiFi connection successful
 */
void CalibreConnectActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected || WiFi.status() != WL_CONNECTED) {
    exitActivity();
    onComplete();
    return;
  }

  if (subActivity) {
    connectedIP = static_cast<WifiSelectionActivity*>(subActivity.get())->getConnectedIP();
  } else {
    connectedIP = WiFi.localIP().toString().c_str();
  }
  connectedSSID = WiFi.SSID().c_str();
  exitActivity();
  startWebServer();
}

/**
 * @brief Initializes and starts the web server for Calibre wireless connection
 */
void CalibreConnectActivity::startWebServer() {
  state = CalibreConnectState::SERVER_STARTING;
  updateRequired = true;

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    INX_SERIAL.printf("[CAL] mDNS started: http://%s.local/\n", HOSTNAME);
  }

  webServer.reset(new LocalServer());
  webServer->begin();

  if (webServer->isRunning()) {
    lastProcessedCompleteAt = webServer->getWsUploadStatus().lastCompleteAt;
    state = CalibreConnectState::SERVER_RUNNING;
    updateRequired = true;
    INX_SERIAL.printf("[CAL] Compatible web server started on HTTP 80, WebSocket 81\n");
  } else {
    webServer.reset();
    state = CalibreConnectState::ERROR;
    updateRequired = true;
    INX_SERIAL.printf("[CAL] Failed to start compatible web server\n");
  }
}

/**
 * @brief Stops the web server and cleans up all related resources
 */
void CalibreConnectActivity::stopWebServer() {
  if (webServer) {
    webServer->stop();
    webServer.reset();
  }
}

/**
 * @brief Main loop processing WiFi connections and HTTP requests
 */
void CalibreConnectActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (state == CalibreConnectState::SERVER_RUNNING && mappedInput.hasTouch()) {
    float tapX = 0.0f;
    float tapY = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) {
      const ButtonBounds disconnect = disconnectBounds(renderer);
      const int x = static_cast<int>(tapX * renderer.getScreenWidth());
      const int y = static_cast<int>(tapY * renderer.getScreenHeight());
      if (contains(disconnect, x, y)) {
        INX_SERIAL.printf("[%lu] [CAL] disconnect requested\n", millis());
        stopWebServer();
        exitActivity();
        onComplete();
        return;
      }
      mappedInput.restoreTouchTapInScreen(renderer, tapX, tapY);
    }
  }

  if (SubPage::closeInput(renderer, mappedInput, onComplete)) return;

  if (webServer && webServer->isRunning() && state == CalibreConnectState::SERVER_RUNNING) {
    webServer->handleClient();

    const LocalServer::WsUploadStatus status = webServer->getWsUploadStatus();
    bool changed = false;
    if (status.inProgress) {
      if (status.received != lastProgressReceived || status.total != lastProgressTotal ||
          status.filename != currentUploadName) {
        lastProgressReceived = status.received;
        lastProgressTotal = status.total;
        currentUploadName = status.filename;
        changed = true;
      }
    } else if (lastProgressReceived != 0 || lastProgressTotal != 0 || !currentUploadName.empty()) {
      lastProgressReceived = 0;
      lastProgressTotal = 0;
      currentUploadName.clear();
      changed = true;
    }

    if (status.lastCompleteAt != 0 && status.lastCompleteAt != lastProcessedCompleteAt) {
      lastCompleteAt = status.lastCompleteAt;
      lastCompleteName = status.lastCompleteName;
      lastProcessedCompleteAt = status.lastCompleteAt;
      if (!lastCompleteName.empty()) receivedFiles.push_back(lastCompleteName);
      changed = true;
    }
    if (lastCompleteAt > 0 && millis() - lastCompleteAt >= 6000) {
      lastCompleteAt = 0;
      lastCompleteName.clear();
      changed = true;
    }
    if (changed) updateRequired = true;
  }

}

/**
 * @brief Background task loop that handles display updates
 */
void CalibreConnectActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      if (state == CalibreConnectState::WIFI_SELECTION) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;
      }
      if (renderingMutex && xSemaphoreTake(renderingMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        render();
        xSemaphoreGive(renderingMutex);
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Main rendering function that dispatches to appropriate state renderers
 */
void CalibreConnectActivity::render() const {
  renderer.syncWriteBufferFromActive();
  renderer.clearScreen();

  int screenWidth = renderer.getScreenWidth();
  int screenHeight = renderer.getScreenHeight();
  int startY = 0;

  if (state == CalibreConnectState::SERVER_RUNNING) {
    renderServerRunning(screenWidth, screenHeight, startY);
  } else if (state == CalibreConnectState::SERVER_STARTING) {
    const int headerBottom = SubPage::header(renderer, "File Transfer", "Calibre");
    const int imageX = (screenWidth - IMAGE_WIDTH) / 2;
    const int imageY = headerBottom;
    renderer.bitmap.icon(kWifiTransferSetup, imageX, imageY, IMAGE_WIDTH, IMAGE_HEIGHT);

    const int panelY = imageY + IMAGE_HEIGHT + IMAGE_GAP;
    const int panelBottom = std::max(panelY + 1, screenHeight - PANEL_BUTTON_GAP);
    for (int py = panelY; py < panelBottom; py += 2) {
      const int offset = ((py - panelY) & 2) != 0 ? 0 : 1;
      for (int px = offset; px < screenWidth; px += 2) renderer.drawPixel(px, py, true);
    }

    const int cardX = CARD_PADDING;
    const int cardY = panelY + CARD_PADDING;
    const int cardWidth = screenWidth - 2 * CARD_PADDING;
    const int cardHeight = UiLayout::LIST_ITEM_HEIGHT;
    const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
    BaseCarousel::renderShadow(renderer, cardX + 4, cardY + 4, cardWidth, cardHeight,
                               HomeTheme::CarouselShadowStyle::Gray);
    renderer.rectangle.fill(cardX, cardY, cardWidth, cardHeight, false, rounded);
    renderer.rectangle.render(cardX, cardY, cardWidth, cardHeight, true, rounded);
    const int font = systemFontId();
    const int textY = cardY + (cardHeight - renderer.text.getLineHeight(font)) / 2;
    renderer.text.centered(font, textY, "Please wait...");

    auto labels = mappedInput.mapLabels("« Back", "", "", "");
  } else if (state == CalibreConnectState::ERROR) {
    const int contentStart = SubPage::header(renderer, "File Transfer", "Calibre");

    int centerY = contentStart + (screenHeight - contentStart) / 2;

    renderer.text.centered(systemFontId(), centerY - 20, "Could not start server");
    renderer.text.centered(systemFontId(), centerY + 10, "Press Back to return");

    auto labels = mappedInput.mapLabels("« Back", "", "", "");
  }

  renderer.displayBuffer();
}

/**
 * @brief Renders the server running state UI with network info and upload progress
 * @param screenWidth Width of the display
 * @param screenHeight Height of the display
 * @param startY Starting Y coordinate for content
 */
void CalibreConnectActivity::renderServerRunning(int screenWidth, int screenHeight, int startY) const {
  (void)startY;

  const int headerBottom = SubPage::header(renderer, "File Transfer", "Calibre");
  const int imageX = (screenWidth - IMAGE_WIDTH) / 2;
  const int imageY = headerBottom;
  renderer.bitmap.icon(kWifiTransferSetup, imageX, imageY, IMAGE_WIDTH, IMAGE_HEIGHT);

  const int panelY = imageY + IMAGE_HEIGHT + IMAGE_GAP;
  const ButtonBounds disconnect = disconnectBounds(renderer);
  const int panelBottom = std::max(panelY + 1, disconnect.y - PANEL_BUTTON_GAP);
  const int panelHeight = std::max(1, panelBottom - panelY);
  for (int py = panelY; py < panelBottom; py += 2) {
    const int offset = ((py - panelY) & 2) != 0 ? 0 : 1;
    for (int px = offset; px < screenWidth; px += 2) renderer.drawPixel(px, py, true);
  }

  const int cardX = CARD_PADDING;
  const int cardWidth = screenWidth - 2 * CARD_PADDING;
  const int font = systemFontId();
  const int lineHeight = renderer.text.getLineHeight(font);
  const int contentX = cardX + CARD_PADDING;
  const bool uploadActive = lastProgressTotal > 0 && lastProgressReceived <= lastProgressTotal;
  const int totalFiles = static_cast<int>(receivedFiles.size()) + (uploadActive ? 1 : 0);
  const bool showInstructions = receivedFiles.empty() && !uploadActive;
  const int instructionCardY = panelY + CARD_PADDING;
  const int instructionWidth = cardWidth - 2 * CARD_PADDING;
  const int listIndent = renderer.text.getWidth(font, "1. ");
  const std::vector<WrappedInstructionLine> installLines = wrapInstruction(
      renderer, font, "1. Download and install the CrossPoint Calibre plugin.", instructionWidth, listIndent);
  const std::vector<WrappedInstructionLine> sendLines = wrapInstruction(
      renderer, font, "2. Choose Send to device in Calibre.", instructionWidth, listIndent);
  const int instructionLineCount = static_cast<int>(installLines.size() + sendLines.size());
  const int instructionCardHeight = CARD_PADDING * 2 + lineHeight + 8 +
                                    instructionLineCount * lineHeight;
  const int listStartY = showInstructions ? instructionCardY + instructionCardHeight + CARD_GAP
                                          : panelY + CARD_PADDING;
  const int rowHeight = UiLayout::LIST_ITEM_HEIGHT;
  const int availableHeight = std::max(0, panelBottom - listStartY - CARD_PADDING);
  const int maxVisibleFiles = std::max(0, (availableHeight + CARD_GAP) / (rowHeight + CARD_GAP));
  const int visibleFiles = std::min(maxVisibleFiles, totalFiles);
  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;

  if (showInstructions) {
    BaseCarousel::renderShadow(renderer, cardX + 4, instructionCardY + 4, cardWidth, instructionCardHeight,
                               HomeTheme::CarouselShadowStyle::Gray);
    renderer.rectangle.fill(cardX, instructionCardY, cardWidth, instructionCardHeight, false, rounded);
    renderer.rectangle.render(cardX, instructionCardY, cardWidth, instructionCardHeight, true, rounded);
    renderer.text.render(font, contentX, instructionCardY + CARD_PADDING, "Instructions", true,
                         EpdFontFamily::BOLD);
    int instructionY = instructionCardY + CARD_PADDING + lineHeight + 4;
    for (const auto& line : installLines) {
      renderer.text.render(font, contentX + line.indent, instructionY, line.text.c_str());
      instructionY += lineHeight;
    }
    instructionY += 4;
    for (const auto& line : sendLines) {
      renderer.text.render(font, contentX + line.indent, instructionY, line.text.c_str());
      instructionY += lineHeight;
    }
  }

  for (int row = 0; row < visibleFiles; ++row) {
      const bool isCurrentUpload = uploadActive && row == 0;
      const int rowY = listStartY + row * (rowHeight + CARD_GAP);
      const int textY = rowY + (rowHeight - lineHeight) / 2;
      const int rightWidth = isCurrentUpload ? renderer.text.getWidth(font, "100%") : SUCCESS_ICON_SIZE;
      const int maxNameWidth = cardWidth - 2 * CARD_PADDING - rightWidth - 20;
      const char* fileName = currentUploadName.c_str();
      if (!isCurrentUpload) {
        const int completedRow = uploadActive ? row - 1 : row;
        const size_t fileIndex = receivedFiles.size() - 1 - static_cast<size_t>(completedRow);
        fileName = receivedFiles[fileIndex].c_str();
      }
      const std::string displayName = renderer.text.truncate(font, fileName, maxNameWidth);
      BaseCarousel::renderShadow(renderer, cardX + 4, rowY + 4, cardWidth, rowHeight,
                                 HomeTheme::CarouselShadowStyle::Gray);
      renderer.rectangle.fill(cardX, rowY, cardWidth, rowHeight, false, rounded);
      renderer.rectangle.render(cardX, rowY, cardWidth, rowHeight, true, rounded);
      renderer.text.render(font, contentX, textY, displayName.c_str());
      if (isCurrentUpload) {
        const int percent = lastProgressTotal == 0
                                ? 0
                                : std::min(100, static_cast<int>((static_cast<uint64_t>(lastProgressReceived) * 100) /
                                                                 lastProgressTotal));
        const std::string progress = std::to_string(percent) + "%";
        renderer.text.render(font, cardX + cardWidth - CARD_PADDING - renderer.text.getWidth(font, progress.c_str()),
                             textY, progress.c_str());
      } else {
        renderer.bitmap.icon(Check, cardX + cardWidth - CARD_PADDING - SUCCESS_ICON_SIZE,
                             rowY + (rowHeight - SUCCESS_ICON_SIZE) / 2, SUCCESS_ICON_SIZE, SUCCESS_ICON_SIZE);
      }
  }

  Button::render(renderer, disconnect, "Disconnect", true, systemFontId());
  mappedInput.mapLabels("« Back", "Disconnect", "", "");
}

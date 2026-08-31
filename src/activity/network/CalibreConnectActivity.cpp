/**
 * @file CalibreConnectActivity.cpp
 * @brief Definitions for CalibreConnectActivity.
 */

#include "CalibreConnectActivity.h"

#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <WiFi.h>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Button.h"
#include "WifiSelectionActivity.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"

namespace {

/**
 * @brief mDNS hostname for device discovery on local network
 */
constexpr const char* HOSTNAME = "inx";

/**
 * @brief Left/right margin for text content
 */
constexpr int CONTENT_MARGIN = 25;

/**
 * @brief Vertical spacing between text lines
 */
constexpr int LINE_SPACING = 28;

/**
 * @brief Small vertical spacing between elements
 */
constexpr int SMALL_SPACING = 25;

/**
 * @brief Large vertical spacing between sections
 */
constexpr int SECTION_SPACING = 40;

ButtonBounds exitButtonBounds(const GfxRenderer& renderer) {
  const int width = Button::width(renderer, "Exit", systemFontId());
  return {renderer.getScreenWidth() - width - 20, renderer.getScreenHeight() - Button::height - 20, width,
          Button::height};
}

bool contains(const ButtonBounds& bounds, const int x, const int y) {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}

/**
 * @brief Truncates a string to a maximum length, adding ellipsis if needed
 * @param str Input string to truncate
 * @param maxLength Maximum allowed length
 * @return Truncated string with ellipsis if original exceeds maxLength
 */
std::string truncateString(const std::string& str, int maxLength) {
  if (str.length() <= maxLength) return str;
  std::string result = str;
  result.replace(maxLength - 3, result.length() - (maxLength - 3), "...");
  return result;
}
}  // namespace

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
  exitRequested = false;

  // Install the Wi-Fi child before starting the Calibre renderer. Starting the parent task first
  // lets it clear/display the Calibre state while WifiSelectionActivity is drawing its scan screen,
  // which leaves the previous screen visible as a ghost on the panel.
  const bool alreadyConnected = WiFi.status() == WL_CONNECTED;
  if (!alreadyConnected) {
    enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                               [this](const bool connected) { onWifiSelectionComplete(connected); }));
  } else {
    connectedIP = WiFi.localIP().toString().c_str();
    connectedSSID = WiFi.SSID().c_str();
  }

  // Keep display rendering off the activity loop while the network server is active.
  xTaskCreate(&CalibreConnectActivity::taskTrampoline, "CalibreConnectTask", 8192, this, 1, &displayTaskHandle);

  if (alreadyConnected) {
    startWebServer();
  }
}

/**
 * @brief Cleans up resources when exiting the activity
 */
void CalibreConnectActivity::onExit() {
  ActivityWithSubactivity::onExit();

  stopWebServer();
  MDNS.end();

  delay(50);
  WiFi.disconnect(false);
  delay(30);
  WiFi.mode(WIFI_OFF);

  if (renderingMutex) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    if (displayTaskHandle) {
      vTaskDelete(displayTaskHandle);
      displayTaskHandle = nullptr;
    }
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }
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

  // Use the shared server implementation used by File Transfer and Hotspot.
  // It provides HTTP 80, WebSocket 81, and UDP discovery on 8134, which is the
  // protocol expected by the CrossPoint Calibre plugin.
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

  if (SubPage::closeInput(renderer, mappedInput, onComplete)) return;

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
      if (contains(exitButtonBounds(renderer), tapX, tapY)) exitRequested = true;
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    exitRequested = true;
    return;
  }

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
      changed = true;
    }
    if (lastCompleteAt > 0 && millis() - lastCompleteAt >= 6000) {
      lastCompleteAt = 0;
      lastCompleteName.clear();
      changed = true;
    }
    if (changed) updateRequired = true;
  }

  if (exitRequested) {
    onComplete();
  }
}

/**
 * @brief Background task loop that handles display updates
 */
void CalibreConnectActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      // WifiSelectionActivity owns the display while the connection picker is active. The
      // Calibre parent has no WIFI_SELECTION rendering of its own; clearing here would overwrite
      // the scan screen and leave the previous page ghosted underneath it.
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
    const int contentStart = SubPage::header(renderer, "Connect to Calibre");

    int centerY = contentStart + (screenHeight - contentStart) / 2;

    renderer.text.centered(systemFontId(), centerY, "Please wait...");

    auto labels = mappedInput.mapLabels("« Exit", "", "", "");
  } else if (state == CalibreConnectState::ERROR) {
    const int contentStart = SubPage::header(renderer, "Connect to Calibre");

    int centerY = contentStart + (screenHeight - contentStart) / 2;

    renderer.text.centered(systemFontId(), centerY - 20, "Could not start server");
    renderer.text.centered(systemFontId(), centerY + 10, "Press Exit to try again");
    Button::render(renderer, exitButtonBounds(renderer), "Exit", true, systemFontId());

    auto labels = mappedInput.mapLabels("« Exit", "", "", "");
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
  const int contentStart = SubPage::header(renderer, "Connect to Calibre");

  int currentY = contentStart + SECTION_SPACING - 10;

  renderer.text.render(systemFontId(), CONTENT_MARGIN, currentY, "Network", true,
                       EpdFontFamily::BOLD);
  currentY += LINE_SPACING;

  std::string ssidInfo = connectedSSID;
  renderer.text.render(systemFontId(), CONTENT_MARGIN, currentY,
                       truncateString(ssidInfo, 34).c_str());
  currentY += LINE_SPACING;

  renderer.text.render(systemFontId(), CONTENT_MARGIN, currentY, connectedIP.c_str());
  currentY += LINE_SPACING * 2;

  renderer.line.render(CONTENT_MARGIN, currentY - 10, screenWidth - CONTENT_MARGIN, currentY - 10);
  currentY += SECTION_SPACING;

  renderer.text.render(systemFontId(), CONTENT_MARGIN, currentY, "Setup", true, EpdFontFamily::BOLD);
  currentY += LINE_SPACING;

  renderer.text.render(systemFontId(), CONTENT_MARGIN, currentY,
                       "1.) Install CrossPoint Reader plugin");
  currentY += SMALL_SPACING;
  renderer.text.render(systemFontId(), CONTENT_MARGIN, currentY, "2.) Be on the same WiFi network");
  currentY += SMALL_SPACING;
  renderer.text.render(systemFontId(), CONTENT_MARGIN, currentY,
                       "3.) In Calibre: \"Send to device\"");
  currentY += SMALL_SPACING + 20;
  renderer.text.render(systemFontId(), CONTENT_MARGIN, currentY,
                       "Keep this screen open while sending");
  currentY += SMALL_SPACING * 2;

  renderer.line.render(CONTENT_MARGIN, currentY - 10, screenWidth - CONTENT_MARGIN, currentY - 10);
  currentY += SECTION_SPACING;

  renderer.text.render(systemFontId(), CONTENT_MARGIN, currentY, "Status", true, EpdFontFamily::BOLD);
  currentY += LINE_SPACING;

  if (lastProgressTotal > 0 && lastProgressReceived <= lastProgressTotal) {
    std::string label = "Receiving";
    if (!currentUploadName.empty()) {
      label += ": " + truncateString(currentUploadName, 30);
    }
    renderer.text.render(systemFontId(), CONTENT_MARGIN, currentY, label.c_str());

    constexpr int barWidth = 300;
    constexpr int barHeight = 16;
    constexpr int barX = (480 - barWidth) / 2;
    ScreenComponents::drawProgressBar(renderer, barX, currentY + 22, barWidth, barHeight, lastProgressReceived,
                                      lastProgressTotal);
    currentY += 50;
  }

  if (lastCompleteAt > 0 && (millis() - lastCompleteAt) < 6000) {
    std::string msg = "Received: " + truncateString(lastCompleteName, 30);
    renderer.text.render(systemFontId(), CONTENT_MARGIN, currentY, msg.c_str());
  }

  Button::render(renderer, exitButtonBounds(renderer), "Exit", true, systemFontId());
  auto labels = mappedInput.mapLabels("« Exit", "", "", "");
}

/**
 * @file WifiSelectionActivity.cpp
 * @brief Definitions for WifiSelectionActivity.
 */

#include "WifiSelectionActivity.h"
#include "system/UiLayout.h"

#include <GfxRenderer.h>
#include <WiFi.h>

extern "C" {
#include <esp_err.h>
#include <esp_wifi.h>
}

#include <algorithm>

#include "activity/page/SubPage.h"
#include "activity/util/KeyboardEntryActivity.h"
#include "state/NetworkCredential.h"
#include "system/Fonts.h"
#include "system/ScreenComponents.h"
#include "system/MappedInputManager.h"

namespace {
constexpr int LIST_ITEM_HEIGHT = Page::LIST_ITEM_HEIGHT;
constexpr uint32_t scanMaxMs = 120;
}

/**
 * @brief Static trampoline function for the display task
 * @param param Pointer to the WifiSelectionActivity instance
 */
void WifiSelectionActivity::taskTrampoline(void* param) {
  auto* self = static_cast<WifiSelectionActivity*>(param);
  self->displayTaskLoop();
}

void WifiSelectionActivity::scanTaskTrampoline(void* param) {
  auto* self = static_cast<WifiSelectionActivity*>(param);
  self->scanTaskLoop();
  vTaskDelete(nullptr);
}

/**
 * @brief Called when entering the activity
 */
void WifiSelectionActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  renderer.syncWriteBufferFromActive();
  renderer.cleanupGrayscaleWithFrameBuffer();

  INX_SERIAL.printf("[%lu] [WIFI] enter mutex=%p mode=%d status=%d scan=%d\n", millis(), renderingMutex,
                 static_cast<int>(WiFi.getMode()), static_cast<int>(WiFi.status()), WiFi.scanComplete());

  state = WifiSelectionState::SCANNING;
  updateRequired = true;
  networkListFullRefreshRequired = true;

  selectedNetworkIndex = 0;
  selectedNetworkVisible = false;
  networks.clear();
  selectedSSID.clear();
  connectedIP.clear();
  connectionError.clear();
  enteredPassword.clear();
  usedSavedPassword = false;
  forgetPromptSelection = 0;
  scanCancelled = false;

  WIFI_STORE.loadFromFile();

  state = WifiSelectionState::SCANNING;

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[32];
  snprintf(macStr, sizeof(macStr), "MAC address: %02x-%02x-%02x-%02x-%02x-%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
           mac[5]);
  cachedMacAddress = std::string(macStr);

  updateRequired = true;

  const BaseType_t taskResult =
      xTaskCreate(&WifiSelectionActivity::taskTrampoline, "WifiSelectionTask", 4096, this, 1, &displayTaskHandle);
  INX_SERIAL.printf("[%lu] [WIFI] display task result=%d handle=%p\n", millis(), static_cast<int>(taskResult),
                 displayTaskHandle);

  startWifiScan();
}

/**
 * @brief Called when exiting the activity
 */
void WifiSelectionActivity::onExit() {
  Activity::onExit();

  INX_SERIAL.printf("[%lu] [WIFI] exit state=%d networks=%u mode=%d status=%d scan=%d\n", millis(),
                 static_cast<int>(state), static_cast<unsigned>(networks.size()), static_cast<int>(WiFi.getMode()),
                 static_cast<int>(WiFi.status()), WiFi.scanComplete());

  scanCancelled = true;
  const esp_err_t stopped = esp_wifi_scan_stop();
  INX_SERIAL.printf("[%lu] [WIFI] exit scan stop=%d (%s)\n", millis(), static_cast<int>(stopped),
                 esp_err_to_name(stopped));

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  networks.clear();
  selectedSSID.clear();
  connectedIP.clear();
  connectionError.clear();
  enteredPassword.clear();
  cachedMacAddress.clear();

  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }

  if (scanTaskHandle) {
    vTaskDelete(scanTaskHandle);
    scanTaskHandle = nullptr;
  }

  xSemaphoreGive(renderingMutex);
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  WiFi.scanDelete();

  exitActivity();
}

/**
 * @brief Starts an asynchronous WiFi network scan
 */
void WifiSelectionActivity::startWifiScan() {
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = WifiSelectionState::SCANNING;
  networks.clear();
  updateRequired = true;
  xSemaphoreGive(renderingMutex);

  WiFi.scanDelete();
  const bool modeSet = WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  const bool disconnected = WiFi.disconnect();
  delay(100);
  scanStartedAt = millis();
  scanCancelled = false;
  const BaseType_t result =
      xTaskCreatePinnedToCore(&WifiSelectionActivity::scanTaskTrampoline, "WifiScanTask", 4096, this, 1,
                              &scanTaskHandle, 1);
  INX_SERIAL.printf("[%lu] [WIFI] native scan task result=%d handle=%p mode-set=%d disconnect=%d dwell=%lums mode=%d status=%d\n",
                 millis(), static_cast<int>(result), scanTaskHandle, modeSet, disconnected,
                 static_cast<unsigned long>(scanMaxMs), static_cast<int>(WiFi.getMode()), static_cast<int>(WiFi.status()));
  if (result != pdPASS) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = WifiSelectionState::NETWORK_LIST;
    updateRequired = true;
    networkListFullRefreshRequired = true;
    xSemaphoreGive(renderingMutex);
  }
}

void WifiSelectionActivity::scanTaskLoop() {
  wifi_scan_config_t config{};
  config.show_hidden = false;
  config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  config.scan_time.active.min = 0;
  config.scan_time.active.max = scanMaxMs;

  const esp_err_t started = esp_wifi_scan_start(&config, true);
  const unsigned long elapsed = millis() - scanStartedAt;
  INX_SERIAL.printf("[%lu] [WIFI] native scan complete result=%d (%s) elapsed=%lums\n", millis(),
                 static_cast<int>(started), esp_err_to_name(started), elapsed);

  std::vector<WifiNetworkInfo> foundNetworks;
  if (started == ESP_OK && !scanCancelled) {
    uint16_t count = 0;
    const esp_err_t countResult = esp_wifi_scan_get_ap_num(&count);
    INX_SERIAL.printf("[%lu] [WIFI] native scan records count=%u count-result=%d (%s)\n", millis(),
                   static_cast<unsigned>(count), static_cast<int>(countResult), esp_err_to_name(countResult));

    if (countResult == ESP_OK && count > 0) {
      std::vector<wifi_ap_record_t> records(count);
      uint16_t recordCapacity = count;
      const esp_err_t recordsResult = esp_wifi_scan_get_ap_records(&recordCapacity, records.data());
      INX_SERIAL.printf("[%lu] [WIFI] native scan records read=%u result=%d (%s)\n", millis(),
                     static_cast<unsigned>(recordCapacity), static_cast<int>(recordsResult),
                     esp_err_to_name(recordsResult));

      if (recordsResult != ESP_OK) {
        recordCapacity = 0;
      }

      foundNetworks.reserve(recordCapacity);
      for (uint16_t index = 0; index < recordCapacity; ++index) {
        const wifi_ap_record_t& record = records[index];
        const std::string ssid(reinterpret_cast<const char*>(record.ssid));
        const int32_t rssi = record.rssi;
        const int channel = record.primary;
        const wifi_auth_mode_t auth = record.authmode;
        INX_SERIAL.printf("[%lu] [WIFI] native scan[%d] ssid=%s rssi=%ld channel=%d auth=%d\n", millis(), index,
                       ssid.c_str(), static_cast<long>(rssi), channel, static_cast<int>(auth));
        if (ssid.empty()) {
          continue;
        }

        auto existing = std::find_if(foundNetworks.begin(), foundNetworks.end(), [&ssid](const auto& network) {
          return network.ssid == ssid;
        });
        if (existing == foundNetworks.end()) {
          WifiNetworkInfo network;
          network.ssid = ssid;
          network.rssi = rssi;
          network.isEncrypted = auth != WIFI_AUTH_OPEN;
          network.hasSavedPassword = WIFI_STORE.hasSavedCredential(network.ssid);
          foundNetworks.push_back(std::move(network));
        } else if (rssi > existing->rssi) {
          existing->rssi = rssi;
          existing->isEncrypted = auth != WIFI_AUTH_OPEN;
        }
      }
    }
  }

  WiFi.scanDelete();

  if (renderingMutex && !scanCancelled) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    if (!scanCancelled) {
      networks = std::move(foundNetworks);
      std::sort(networks.begin(), networks.end(), [](const WifiNetworkInfo& left, const WifiNetworkInfo& right) {
        if (left.hasSavedPassword != right.hasSavedPassword) {
          return left.hasSavedPassword;
        }
        return left.rssi > right.rssi;
      });
      state = WifiSelectionState::NETWORK_LIST;
      selectedNetworkIndex = 0;
      selectedNetworkVisible = false;
      updateRequired = true;
      networkListFullRefreshRequired = true;
      INX_SERIAL.printf("[%lu] [WIFI] native list published networks=%u\n", millis(),
                     static_cast<unsigned>(networks.size()));
    }
    xSemaphoreGive(renderingMutex);
  }

  scanTaskHandle = nullptr;
}

/**
 * @brief Selects a network from the list to connect to
 * @param index Index of the network to select
 */
void WifiSelectionActivity::selectNetwork(const int index) {
  if (index < 0 || index >= static_cast<int>(networks.size())) {
    return;
  }

  const auto& network = networks[index];
  INX_SERIAL.printf("[%lu] [WIFI] select index=%d ssid=%s encrypted=%d saved=%d\n", millis(), index,
                 network.ssid.c_str(), network.isEncrypted, network.hasSavedPassword);
  selectedSSID = network.ssid;
  selectedRequiresPassword = network.isEncrypted;
  usedSavedPassword = false;
  enteredPassword.clear();

  const auto* savedCred = WIFI_STORE.findCredential(selectedSSID);
  if (savedCred && !savedCred->password.empty()) {
    enteredPassword = savedCred->password;
    usedSavedPassword = true;
    attemptConnection();
    return;
  }

  if (selectedRequiresPassword) {
    state = WifiSelectionState::PASSWORD_ENTRY;
    INX_SERIAL.printf("[%lu] [WIFI] opening password keyboard ssid=%s\n", millis(), selectedSSID.c_str());
    const unsigned long lockStartedAt = millis();
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    INX_SERIAL.printf("[%lu] [WIFI] password render lock wait=%lums\n", millis(), millis() - lockStartedAt);
    enterNewActivity(new KeyboardEntryActivity(
        renderer, mappedInput, "Enter WiFi Password", "", 50, 64, false,
        [this](const std::string& text) {
          enteredPassword = text;
          exitActivity();
        },
        [this] {
          state = WifiSelectionState::NETWORK_LIST;
          updateRequired = true;
          exitActivity();
        }));
    INX_SERIAL.printf("[%lu] [WIFI] password keyboard entered\n", millis());
    updateRequired = true;
    xSemaphoreGive(renderingMutex);
  } else {
    attemptConnection();
  }
}

/**
 * @brief Attempts to connect to the selected network
 */
void WifiSelectionActivity::attemptConnection() {
  state = WifiSelectionState::CONNECTING;
  connectionStartTime = millis();
  connectedIP.clear();
  connectionError.clear();
  updateRequired = true;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  if (selectedRequiresPassword && !enteredPassword.empty()) {
    WiFi.begin(selectedSSID.c_str(), enteredPassword.c_str());
  } else {
    WiFi.begin(selectedSSID.c_str());
  }
}

/**
 * @brief Checks the status of an ongoing connection attempt
 */
void WifiSelectionActivity::checkConnectionStatus() {
  if (state != WifiSelectionState::CONNECTING) {
    return;
  }

  const wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    connectedIP = ipStr;

    if (!usedSavedPassword && !enteredPassword.empty()) {
      WIFI_STORE.addCredential(selectedSSID, enteredPassword);
    }
    onComplete(true);
    return;
  }

  if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
    connectionError = "Error: General failure";
    if (status == WL_NO_SSID_AVAIL) {
      connectionError = "Error: Network not found";
    }
    state = WifiSelectionState::CONNECTION_FAILED;
    updateRequired = true;
    return;
  }

  if (millis() - connectionStartTime > 15000) {
    WiFi.disconnect();
    connectionError = "Error: Connection timeout";
    state = WifiSelectionState::CONNECTION_FAILED;
    updateRequired = true;
    return;
  }
}

bool WifiSelectionActivity::handleTouchInput() {
  if (!mappedInput.hasTouch()) {
    return false;
  }

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
    return false;
  }

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int tapX = std::max(0, std::min(screenWidth - 1, static_cast<int>(tapNx * screenWidth)));
  const int tapY = std::max(0, std::min(screenHeight - 1, static_cast<int>(tapNy * screenHeight)));
  const int dividerY = UiLayout::PAGE_HEADER_HEIGHT;

  INX_SERIAL.printf("[STICKY][WIFI TOUCH] state=%d tap=(%d,%d) normalized=(%.3f,%.3f)\n",
                 static_cast<int>(state), tapX, tapY, tapNx, tapNy);

  const bool headerBackHit = ScreenComponents::pageHeaderBackButtonHit(tapX, tapY) ||
                             (tapY < dividerY && tapX >= screenWidth - 96);
  if (headerBackHit) {
    INX_SERIAL.printf("[%lu] [WIFI] touch exit\n", millis());
    onComplete(false);
    return true;
  }

  if (state == WifiSelectionState::NETWORK_LIST) {
    const int listStartY = dividerY;
    const int visibleAreaHeight = screenHeight - listStartY - 80;
    const int maxVisibleNetworks = std::max(1, visibleAreaHeight / LIST_ITEM_HEIGHT);
    int scrollOffset = 0;
    if (selectedNetworkIndex >= maxVisibleNetworks) {
      scrollOffset = selectedNetworkIndex - maxVisibleNetworks + 1;
    }

    if (tapY >= listStartY && tapY < listStartY + maxVisibleNetworks * LIST_ITEM_HEIGHT) {
      const int visibleRow = (tapY - listStartY) / LIST_ITEM_HEIGHT;
      const int networkIndex = scrollOffset + visibleRow;
      if (networkIndex >= 0 && networkIndex < static_cast<int>(networks.size())) {
        selectedNetworkIndex = networkIndex;
        selectNetwork(networkIndex);
        return true;
      }
    }

    if (networks.empty() && tapY >= listStartY) {
      startWifiScan();
      return true;
    }
    return true;
  }

  if (state == WifiSelectionState::FORGET_PROMPT) {
    const int promptY = dividerY + 30;
    const int buttonY = promptY + 50;
    constexpr int buttonWidth = 120;
    constexpr int buttonSpacing = 30;
    const int startX = (screenWidth - (buttonWidth * 2 + buttonSpacing)) / 2;
    if (tapY >= buttonY - 20 && tapY < buttonY + 25) {
      if (tapX >= startX && tapX < startX + buttonWidth) {
        forgetPromptSelection = 0;
      } else if (tapX >= startX + buttonWidth + buttonSpacing &&
                 tapX < startX + buttonWidth * 2 + buttonSpacing) {
        forgetPromptSelection = 1;
      } else {
        return true;
      }

      if (forgetPromptSelection == 1) {
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        WIFI_STORE.removeCredential(selectedSSID);
        xSemaphoreGive(renderingMutex);
        const auto network = find_if(networks.begin(), networks.end(),
                                     [this](const WifiNetworkInfo& net) { return net.ssid == selectedSSID; });
        if (network != networks.end()) {
          network->hasSavedPassword = false;
        }
      }
      state = WifiSelectionState::NETWORK_LIST;
      updateRequired = true;
      return true;
    }
    return true;
  }

  if (state == WifiSelectionState::CONNECTION_FAILED && tapY >= dividerY) {
    if (usedSavedPassword) {
      state = WifiSelectionState::FORGET_PROMPT;
      forgetPromptSelection = 0;
    } else {
      state = WifiSelectionState::NETWORK_LIST;
    }
    updateRequired = true;
    return true;
  }

  return false;
}

/**
 * @brief Main loop function called repeatedly while activity is active
 */
void WifiSelectionActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (SubPage::closeInput(renderer, mappedInput, [this]() { onComplete(false); })) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onComplete(false);
    return;
  }

  if (handleTouchInput()) {
    return;
  }

  if (state == WifiSelectionState::SCANNING) {
    return;
  }

  if (state == WifiSelectionState::CONNECTING) {
    checkConnectionStatus();
    return;
  }

  if (state == WifiSelectionState::PASSWORD_ENTRY) {
    attemptConnection();
    return;
  }

  if (state == WifiSelectionState::FORGET_PROMPT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      forgetPromptSelection = (forgetPromptSelection + 1) % 2;
      updateRequired = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
               mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      forgetPromptSelection = (forgetPromptSelection + 1) % 2;
      updateRequired = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (forgetPromptSelection == 1) {
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        WIFI_STORE.removeCredential(selectedSSID);
        xSemaphoreGive(renderingMutex);
        const auto network = find_if(networks.begin(), networks.end(),
                                     [this](const WifiNetworkInfo& net) { return net.ssid == selectedSSID; });
        if (network != networks.end()) {
          network->hasSavedPassword = false;
        }
      }
      state = WifiSelectionState::NETWORK_LIST;
      updateRequired = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = WifiSelectionState::NETWORK_LIST;
      updateRequired = true;
    }
    return;
  }

  if (state == WifiSelectionState::CONNECTED) {
    onComplete(true);
    return;
  }

  if (state == WifiSelectionState::CONNECTION_FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (usedSavedPassword) {
        state = WifiSelectionState::FORGET_PROMPT;
        forgetPromptSelection = 0;
      } else {
        state = WifiSelectionState::NETWORK_LIST;
      }
      updateRequired = true;
      return;
    }
  }

  if (state == WifiSelectionState::NETWORK_LIST) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      onComplete(false);
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (!networks.empty()) {
        selectNetwork(selectedNetworkIndex);
      } else {
        startWifiScan();
      }
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (!networks.empty()) {
        selectedNetworkIndex = (selectedNetworkIndex + static_cast<int>(networks.size()) - 1) %
                               static_cast<int>(networks.size());
        selectedNetworkVisible = true;
        updateRequired = true;
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (!networks.empty()) {
        selectedNetworkIndex = (selectedNetworkIndex + 1) % static_cast<int>(networks.size());
        selectedNetworkVisible = true;
        updateRequired = true;
      }
    }
  }
}

/**
 * @brief Main display task loop running on separate thread
 */
void WifiSelectionActivity::displayTaskLoop() {
  while (true) {
    if (subActivity) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    if (state != WifiSelectionState::PASSWORD_ENTRY && updateRequired) {
      updateRequired = false;
      const WifiSelectionState renderState = state;
      const bool fullRefresh = networkListFullRefreshRequired;
      networkListFullRefreshRequired = false;
      const unsigned long renderStartedAt = millis();
      INX_SERIAL.printf("[%lu] [WIFI] render start state=%d\n", renderStartedAt, static_cast<int>(renderState));
      render(fullRefresh);
      INX_SERIAL.printf("[%lu] [WIFI] render complete state=%d elapsed=%lums\n", millis(),
                     static_cast<int>(renderState), millis() - renderStartedAt);
    }
    xSemaphoreGive(renderingMutex);
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Renders the current screen content
 */
void WifiSelectionActivity::render(const bool fullRefresh) const {
  renderer.clearScreen();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int startY = 0;

  switch (state) {
    case WifiSelectionState::SCANNING:
      renderScanning(screenWidth, screenHeight, startY);
      break;
    case WifiSelectionState::NETWORK_LIST:
      renderNetworkList(screenWidth, screenHeight, startY);
      break;
    case WifiSelectionState::CONNECTING:
      renderConnecting(screenWidth, screenHeight, startY);
      break;
    case WifiSelectionState::CONNECTION_FAILED:
      renderConnectionFailed(screenWidth, screenHeight, startY);
      break;
    case WifiSelectionState::FORGET_PROMPT:
      renderForgetPrompt(screenWidth, screenHeight, startY);
      break;
  }

  renderer.displayBufferAsync(fullRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  renderer.syncWriteBufferFromActive();
}

/**
 * @brief Renders the scanning state screen
 * @param screenWidth Width of the screen
 * @param screenHeight Height of the screen
 * @param startY Starting Y coordinate for content
 */
void WifiSelectionActivity::renderScanning(const int screenWidth, const int screenHeight, const int startY) const {
  const int dividerY = SubPage::header(renderer, "WiFi Networks");

  const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
  renderer.text.centered(MONTSERRAT_10_FONT_ID, centerY, "Searching for connections...");

  const auto labels = mappedInput.mapLabels("« Back", "", "", "");
}

/**
 * @brief Renders the network list screen
 * @param screenWidth Width of the screen
 * @param screenHeight Height of the screen
 * @param startY Starting Y coordinate for content
 */
void WifiSelectionActivity::renderNetworkList(int screenWidth, int screenHeight, int startY) const {
  const int dividerY = SubPage::header(renderer, "WiFi Networks");

  const int listStartY = dividerY;
  const int visibleAreaHeight = screenHeight - listStartY - 80;

  if (networks.empty()) {
    const int centerY = listStartY + (visibleAreaHeight / 2);
    renderer.text.centered(MONTSERRAT_10_FONT_ID, centerY - 20, "No networks found");
    renderer.text.centered(MONTSERRAT_10_FONT_ID, centerY + 10, "Press Connect to scan again");
  } else {
    const int maxVisibleNetworks = visibleAreaHeight / LIST_ITEM_HEIGHT;

    int scrollOffset = 0;
    if (selectedNetworkIndex >= maxVisibleNetworks) {
      scrollOffset = selectedNetworkIndex - maxVisibleNetworks + 1;
    }

    int displayIndex = 0;
    for (size_t i = scrollOffset; i < networks.size() && displayIndex < maxVisibleNetworks; i++, displayIndex++) {
      const int itemY = listStartY + displayIndex * LIST_ITEM_HEIGHT;
      const auto& network = networks[i];
      const bool isSelected = selectedNetworkVisible && (static_cast<int>(i) == selectedNetworkIndex);

      if (isSelected) {
        renderer.rectangle.fill(0, itemY, screenWidth, LIST_ITEM_HEIGHT, static_cast<int>(GfxRenderer::FillTone::Ink));
      }

      std::string displayName = network.ssid;
      if (displayName.length() > 25) {
        displayName.replace(22, displayName.length() - 22, "...");
      }

      const int textX = 20;
      const int titleY = itemY + 20;

      renderer.text.render(MONTSERRAT_10_FONT_ID, textX, titleY, displayName.c_str(), !isSelected);

      if (network.isEncrypted) {
        int lockTextX = textX + renderer.text.getWidth(MONTSERRAT_10_FONT_ID, displayName.c_str()) + 10;
        if (lockTextX < screenWidth - 150) {
          renderer.text.render(MONTSERRAT_8_FONT_ID, lockTextX, titleY + 2, "(Locked)", !isSelected);
        }
      }

      drawWifiIcon(screenWidth - 60, itemY + 15, network.rssi, isSelected);

      if (network.hasSavedPassword) {
        renderer.text.render(MONTSERRAT_10_FONT_ID, screenWidth - 80, itemY + 15, "+", !isSelected);
      }

      if (i < networks.size() - 1) {
        renderer.line.render(0, itemY + LIST_ITEM_HEIGHT - 1, screenWidth, itemY + LIST_ITEM_HEIGHT - 1, true,
                             LineRender::Style::Dotted);
      }
    }

    if (scrollOffset > 0) {
      renderer.text.render(MONTSERRAT_8_FONT_ID, screenWidth - 15, listStartY, "^");
    }
    if (scrollOffset + maxVisibleNetworks < static_cast<int>(networks.size())) {
      renderer.text.render(MONTSERRAT_8_FONT_ID, screenWidth - 15,
                           listStartY + maxVisibleNetworks * LIST_ITEM_HEIGHT, "v");
    }

    char countStr[32];
    snprintf(countStr, sizeof(countStr), "%zu networks found", networks.size());
    renderer.text.render(MONTSERRAT_8_FONT_ID, 20, screenHeight - 90, countStr);
    renderer.text.render(MONTSERRAT_8_FONT_ID, 20, screenHeight - 105, cachedMacAddress.c_str());
  }

  const auto labels = mappedInput.mapLabels("« Back", "Connect", "", "");
}

/**
 * @brief Renders the connecting state screen
 * @param screenWidth Width of the screen
 * @param screenHeight Height of the screen
 * @param startY Starting Y coordinate for content
 */
void WifiSelectionActivity::renderConnecting(const int screenWidth, const int screenHeight, const int startY) const {
  const int dividerY = SubPage::header(renderer, "WiFi Networks");

  const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;

  std::string ssidInfo = selectedSSID;
  std::string connect = "Connecting to";
  if (ssidInfo.length() > 25) {
    ssidInfo.replace(22, ssidInfo.length() - 22, "...");
  }
  renderer.text.centered(MONTSERRAT_12_FONT_ID, centerY - 50, connect.c_str());
  renderer.text.centered(MONTSERRAT_12_FONT_ID, centerY - 20, ssidInfo.c_str());
  renderer.text.centered(MONTSERRAT_12_FONT_ID, centerY + 20, "Please wait...");

  const auto labels = mappedInput.mapLabels("", "", "", "");
}

/**
 * @brief Renders the connection failed screen
 * @param screenWidth Width of the screen
 * @param screenHeight Height of the screen
 * @param startY Starting Y coordinate for content
 */
void WifiSelectionActivity::renderConnectionFailed(const int screenWidth, const int screenHeight,
                                                   const int startY) const {
  const int dividerY = SubPage::header(renderer, "WiFi Networks");

  const int errorY = dividerY + 40;
  renderer.text.centered(MONTSERRAT_10_FONT_ID, errorY - 20, connectionError.c_str());

  std::string ssidInfo = "Network: " + selectedSSID;
  if (ssidInfo.length() > 25) {
    ssidInfo.replace(22, ssidInfo.length() - 22, "...");
  }
  renderer.text.centered(MONTSERRAT_10_FONT_ID, errorY + 10, ssidInfo.c_str());

  const auto labels = mappedInput.mapLabels("« Back", "Continue", "", "");
}

/**
 * @brief Renders the forget network prompt screen
 * @param screenWidth Width of the screen
 * @param screenHeight Height of the screen
 * @param startY Starting Y coordinate for content
 */
void WifiSelectionActivity::renderForgetPrompt(const int screenWidth, const int screenHeight, const int startY) const {
  const int dividerY = SubPage::header(renderer, "WiFi Networks");

  const int promptY = dividerY + 30;
  renderer.text.centered(MONTSERRAT_10_FONT_ID, promptY, "Forget network and remove saved password?");

  const int buttonY = promptY + 50;
  constexpr int buttonWidth = 120;
  constexpr int buttonSpacing = 30;
  constexpr int totalWidth = buttonWidth * 2 + buttonSpacing;
  const int startX = (screenWidth - totalWidth) / 2;

  if (forgetPromptSelection == 0) {
    renderer.text.render(MONTSERRAT_10_FONT_ID, startX, buttonY, "[Cancel]");
  } else {
    renderer.text.render(MONTSERRAT_10_FONT_ID, startX + 4, buttonY, "Cancel");
  }

  if (forgetPromptSelection == 1) {
    renderer.text.render(MONTSERRAT_10_FONT_ID, startX + buttonWidth + buttonSpacing, buttonY,
                         "[Forget network]");
  } else {
    renderer.text.render(MONTSERRAT_10_FONT_ID, startX + buttonWidth + buttonSpacing + 4, buttonY,
                         "Forget network");
  }

  const auto labels = mappedInput.mapLabels("« Back", "Select", "Left", "Right");
}

/**
 * @brief Draws a WiFi signal strength icon
 * @param x X coordinate for the icon
 * @param y Y coordinate for the icon
 * @param rssi Signal strength in dBm
 * @param isSelected Whether the current item is selected
 */
void WifiSelectionActivity::drawWifiIcon(int x, int y, int32_t rssi, bool isSelected) const {
  int bar1Height = 7;
  int bar2Height = 14;
  int bar3Height = 21;
  int bar4Height = 28;
  int barWidth = 5;

  int maxHeight = bar4Height;
  int startY = y + (28 - maxHeight) / 2;

  int visibleBars = 0;
  if (rssi >= -80) visibleBars = 1;
  if (rssi >= -70) visibleBars = 2;
  if (rssi >= -60) visibleBars = 3;
  if (rssi >= -50) visibleBars = 4;

  bool drawColor = !isSelected;

  int bar1Y = startY + (maxHeight - bar1Height);
  int bar2Y = startY + (maxHeight - bar2Height);
  int bar3Y = startY + (maxHeight - bar3Height);
  int bar4Y = startY;

  if (visibleBars >= 1) {
    renderer.rectangle.fill(x, bar1Y, barWidth, bar1Height, static_cast<int>(drawColor));
  } else {
    renderer.rectangle.render(x, bar1Y, barWidth, bar1Height, drawColor);
  }

  if (visibleBars >= 2) {
    renderer.rectangle.fill(x + 10, bar2Y, barWidth, bar2Height, static_cast<int>(drawColor));
  } else {
    renderer.rectangle.render(x + 10, bar2Y, barWidth, bar2Height, drawColor);
  }

  if (visibleBars >= 3) {
    renderer.rectangle.fill(x + 20, bar3Y, barWidth, bar3Height, static_cast<int>(drawColor));
  } else {
    renderer.rectangle.render(x + 20, bar3Y, barWidth, bar3Height, drawColor);
  }

  if (visibleBars >= 4) {
    renderer.rectangle.fill(x + 30, bar4Y, barWidth, bar4Height, static_cast<int>(drawColor));
  } else {
    renderer.rectangle.render(x + 30, bar4Y, barWidth, bar4Height, drawColor);
  }
}

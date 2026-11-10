/**
 * @file WifiSelectionActivity.cpp
 * @brief Definitions for WifiSelectionActivity.
 */

#include "WifiSelectionActivity.h"
#include "system/UiLayout.h"

#include <GfxRenderer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <qrcode.h>

extern "C" {
#include <esp_err.h>
#include <esp_wifi.h>
}

#include <algorithm>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Button.h"
#include "activity/page/components/global/Paginator.h"
#include "activity/page/components/widget/BaseCarousel.h"
#include "activity/settings/SettingsListLayout.h"
#include "activity/util/KeyboardEntryActivity.h"
#include "state/NetworkCredential.h"
#include "state/SystemSetting.h"
#include "images/WifiLock.h"
#if FREEINK_DEVICE_X4PRO
#include "images/X4ProWifiTransferSetup.h"
#elif FREEINK_DEVICE_STICKY || FREEINK_DEVICE_MURPHY_M4
#include "images/MurphyStickyWifiTransferSetup.h"
#else
#include "images/WifiTransferSetup.h"
#endif
#include "system/Fonts.h"
#include "system/LanguageManager.h"
#include "system/ScreenComponents.h"
#include "system/MappedInputManager.h"

namespace {
constexpr int LIST_ITEM_HEIGHT = SettingsListLayout::kRowHeight;
constexpr int LIST_DIVIDER_INSET = SettingsListLayout::kDividerInset;
constexpr int LANDING_IMAGE_WIDTH = 400;
constexpr int LANDING_IMAGE_HEIGHT = 200;
constexpr int LANDING_IMAGE_GAP = 0;
constexpr int INITIAL_PANEL_GAP = 15;
constexpr int CONNECTION_PANEL_GAP = 15;
constexpr int LANDING_CARD_PADDING = 20;
constexpr int LANDING_CARD_HEIGHT = 120;
constexpr int LANDING_CARD_GAP = 12;
constexpr int LANDING_ACTION_MARGIN = 20;
constexpr int LANDING_BUTTON_GAP = 20;
constexpr int LANDING_PANEL_BUTTON_GAP = 20;
constexpr int TRANSFER_QR_VERSION = 3;
constexpr int TRANSFER_QR_PIXEL_SIZE = 6;
constexpr int TRANSFER_QR_SIZE = TRANSFER_QR_PIXEL_SIZE * (17 + 4 * TRANSFER_QR_VERSION);
constexpr uint32_t scanMaxMs = 120;
// Wi-Fi renders translated labels and may resolve the first non-ASCII glyph by
// loading a streamed language font. TextRender also uses a bounded glyph buffer
// on the task stack, so the old 4 KB stack was not sufficient for CJK packages.
constexpr uint32_t WIFI_DISPLAY_TASK_STACK = 8192;

#if FREEINK_DEVICE_X4PRO
const uint8_t* const kWifiTransferSetup = X4ProWifiTransferSetup;
#elif FREEINK_DEVICE_STICKY || FREEINK_DEVICE_MURPHY_M4
const uint8_t* const kWifiTransferSetup = MurphyStickyWifiTransferSetup;
#else
const uint8_t* const kWifiTransferSetup = WifiTransferSetup;
#endif

ButtonBounds landingConnectBounds(const GfxRenderer& renderer, const bool hasNetworks) {
  const char* label = hasNetworks ? "Connect" : "Search";
  const int width = Button::width(renderer, label, systemFontId());
  return {renderer.getScreenWidth() - LANDING_ACTION_MARGIN - width,
          renderer.getScreenHeight() - LANDING_ACTION_MARGIN - Button::height, width, Button::height};
}

ButtonBounds landingRefreshBounds(const GfxRenderer& renderer, const bool hasNetworks) {
  const ButtonBounds connect = landingConnectBounds(renderer, hasNetworks);
  const int width = Button::width(renderer, "Refresh", systemFontId());
  return {connect.x - LANDING_BUTTON_GAP - width, connect.y, width, Button::height};
}

ButtonBounds connectedDisconnectBounds(const GfxRenderer& renderer) {
  const int width = Button::width(renderer, "Disconnect", systemFontId());
  return {renderer.getScreenWidth() - LANDING_ACTION_MARGIN - width,
          renderer.getScreenHeight() - LANDING_ACTION_MARGIN - Button::height, width, Button::height};
}

int landingPanelBottom(const GfxRenderer& renderer, const bool hasNetworks) {
  return landingConnectBounds(renderer, hasNetworks).y - LANDING_PANEL_BUTTON_GAP;
}

int landingCardHeight(const int panelHeight) {
  return std::max(1, std::min(LIST_ITEM_HEIGHT, panelHeight - LANDING_CARD_PADDING));
}

int landingVisibleRows(const int panelHeight) {
  const int itemHeight = landingCardHeight(panelHeight);
  return std::max(1, (panelHeight - LANDING_CARD_PADDING + LANDING_CARD_GAP) /
                          (itemHeight + LANDING_CARD_GAP));
}
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
  vTaskDeleteWithCaps(nullptr);
}

/**
 * @brief Called when entering the activity
 */
void WifiSelectionActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  INX_SERIAL.printf("[%lu] [WIFI] enter mutex=%p mode=%d status=%d scan=%d\n", millis(), renderingMutex,
                 static_cast<int>(WiFi.getMode()), static_cast<int>(WiFi.status()), WiFi.scanComplete());

  state = WifiSelectionState::SAVED_NETWORKS;
  updateRequired = true;

  selectedNetworkIndex = 0;
  networkPage = 0;
  selectedNetworkVisible = false;
  networks.clear();
  selectedSSID.clear();
  connectedIP.clear();
  connectionError.clear();
  enteredPassword.clear();
  usedSavedPassword = false;
  completionNotified = false;
  forgetPromptSelection = 0;
  scanCancelled = false;

  WIFI_STORE.loadFromFile();

  networks.clear();
  networks.reserve(WIFI_STORE.getCredentials().size());
  for (const auto& credential : WIFI_STORE.getCredentials()) {
    // Saved credentials are intentionally shown without probing the radio. The
    // real signal/encryption details are populated if the user explicitly scans.
    networks.push_back({credential.ssid, -100, true, true});
  }
  selectedNetworkIndex = 0;
  networkPage = 0;
  selectedNetworkVisible = !networks.empty();

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[32];
  snprintf(macStr, sizeof(macStr), LanguageManager::translateText("MAC address: %02x-%02x-%02x-%02x-%02x-%02x"),
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  cachedMacAddress = std::string(macStr);

  // The saved-network landing page is reserved for file-transfer services.
  // Other Wi-Fi consumers keep the original behavior and scan immediately.
  if (!usesFileTransferLayout()) {
    startWifiScan();
  } else {
    // USB paints its first screen synchronously from onEnter(). Do the same
    // for the saved-network landing state before the display task starts.
    render();
    updateRequired = false;
  }

  const BaseType_t taskResult = xTaskCreateWithCaps(
      &WifiSelectionActivity::taskTrampoline, "WifiSelectionTask", WIFI_DISPLAY_TASK_STACK, this, 1,
      &displayTaskHandle, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  INX_SERIAL.printf("[%lu] [WIFI] display task result=%d handle=%p\n", millis(), static_cast<int>(taskResult),
                 displayTaskHandle);

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
    vTaskDeleteWithCaps(displayTaskHandle);
    displayTaskHandle = nullptr;
  }

  if (scanTaskHandle) {
    vTaskDeleteWithCaps(scanTaskHandle);
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
  networkPage = 0;
  updateRequired = true;
  xSemaphoreGive(renderingMutex);

  WiFi.scanDelete();
  const bool modeSet = WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  const bool disconnected = WiFi.disconnect();
  delay(100);
  scanStartedAt = millis();
  scanCancelled = false;
  const BaseType_t result = xTaskCreatePinnedToCoreWithCaps(
      &WifiSelectionActivity::scanTaskTrampoline, "WifiScanTask", 4096, this, 1, &scanTaskHandle, 1,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  INX_SERIAL.printf("[%lu] [WIFI] native scan task result=%d handle=%p mode-set=%d disconnect=%d dwell=%lums mode=%d status=%d\n",
                 millis(), static_cast<int>(result), scanTaskHandle, modeSet, disconnected,
                 static_cast<unsigned long>(scanMaxMs), static_cast<int>(WiFi.getMode()), static_cast<int>(WiFi.status()));
  if (result != pdPASS) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = WifiSelectionState::NETWORK_LIST;
    updateRequired = true;
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
      networkPage = 0;
      selectedNetworkVisible = false;
      updateRequired = true;
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
    state = WifiSelectionState::CONNECTED;
    updateRequired = true;
    if (!completionNotified) {
      completionNotified = true;
      onComplete(true);
    }
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

  if (usesFileTransferLayout() &&
      (state == WifiSelectionState::SAVED_NETWORKS || state == WifiSelectionState::SCANNING ||
       state == WifiSelectionState::NETWORK_LIST)) {
    const bool swipeUp = mappedInput.wasTouchSwipeUpForRenderer(renderer);
    const bool swipeDown = mappedInput.wasTouchSwipeDownForRenderer(renderer);
    if (swipeUp || swipeDown) {
      if (state != WifiSelectionState::SCANNING && !networks.empty()) {
        const int panelHeight = std::max(1, landingPanelBottom(renderer, true) -
                                             (UiLayout::PAGE_HEADER_HEIGHT + LANDING_IMAGE_HEIGHT));
        const int visibleRows = landingVisibleRows(panelHeight);
        const int pageCount = std::max(1, (static_cast<int>(networks.size()) + visibleRows - 1) / visibleRows);
        networkPage = std::max(0, std::min(pageCount - 1, networkPage + (swipeUp ? 1 : -1)));
        selectedNetworkIndex = std::min(static_cast<int>(networks.size()) - 1, networkPage * visibleRows);
        selectedNetworkVisible = true;
        updateRequired = true;
      }
      return true;
    }
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

  if (state == WifiSelectionState::CONNECTED) {
    const ButtonBounds disconnect = connectedDisconnectBounds(renderer);
    if (tapX >= disconnect.x && tapX < disconnect.x + disconnect.width && tapY >= disconnect.y &&
        tapY < disconnect.y + disconnect.height) {
      INX_SERIAL.printf("[%lu] [WIFI] disconnect requested\n", millis());
      WiFi.disconnect();
      onComplete(false);
    }
    return true;
  }

  if (!usesFileTransferLayout()) {
    if (state == WifiSelectionState::SCANNING) {
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
      if (!networks.empty() && tapY >= listStartY &&
          tapY < listStartY + maxVisibleNetworks * LIST_ITEM_HEIGHT) {
        const int index = scrollOffset + (tapY - listStartY) / LIST_ITEM_HEIGHT;
        if (index >= 0 && index < static_cast<int>(networks.size())) {
          selectedNetworkIndex = index;
          selectedNetworkVisible = true;
          selectNetwork(index);
        }
      }
      return true;
    }
  }

  if (usesFileTransferLayout() &&
      (state == WifiSelectionState::SAVED_NETWORKS || state == WifiSelectionState::SCANNING ||
       state == WifiSelectionState::NETWORK_LIST)) {
    const int cardTop = dividerY + LANDING_IMAGE_GAP + LANDING_IMAGE_HEIGHT + INITIAL_PANEL_GAP;
    const int panelHeight = std::max(1, landingPanelBottom(renderer, !networks.empty()) - cardTop);
    const int listStartY = cardTop + LANDING_CARD_PADDING;
    const int itemHeight = landingCardHeight(panelHeight);
    const int maxVisibleNetworks = landingVisibleRows(panelHeight);
    const ButtonBounds refresh = landingRefreshBounds(renderer, !networks.empty());
    const ButtonBounds connect = landingConnectBounds(renderer, !networks.empty());

    if (tapX >= refresh.x && tapX < refresh.x + refresh.width && tapY >= refresh.y && tapY < refresh.y + refresh.height) {
      if (state != WifiSelectionState::SCANNING) startWifiScan();
      return true;
    }

    if (tapX >= connect.x && tapX < connect.x + connect.width && tapY >= connect.y && tapY < connect.y + connect.height) {
      if (state == WifiSelectionState::SCANNING) return true;
      if (networks.empty()) {
        startWifiScan();
      } else {
        selectNetwork(selectedNetworkIndex);
      }
      return true;
    }

    if (state != WifiSelectionState::SCANNING && !networks.empty() && tapY >= listStartY) {
      const int visibleRow = (tapY - listStartY) / (itemHeight + LANDING_CARD_GAP);
      const int itemY = listStartY + visibleRow * (itemHeight + LANDING_CARD_GAP);
      if (visibleRow >= 0 && visibleRow < maxVisibleNetworks && tapY < itemY + itemHeight) {
        const int index = networkPage * maxVisibleNetworks + visibleRow;
        if (index >= 0 && index < static_cast<int>(networks.size())) {
          selectedNetworkIndex = index;
          selectedNetworkVisible = true;
          updateRequired = true;
        }
      }
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

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
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
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      forgetPromptSelection = (forgetPromptSelection + 1) % 2;
      updateRequired = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
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
    // The success callback is sent once from checkConnectionStatus(). Keep the
    // connected card visible so File Transfer can start its server underneath
    // this activity without replacing the page.
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

  }

  if (state == WifiSelectionState::SAVED_NETWORKS) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (networks.empty()) {
        startWifiScan();
      } else {
        selectNetwork(selectedNetworkIndex);
      }
      return;
    }

    return;
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
      const unsigned long renderStartedAt = millis();
      INX_SERIAL.printf("[%lu] [WIFI] render start state=%d\n", renderStartedAt, static_cast<int>(renderState));
      render();
      INX_SERIAL.printf("[%lu] [WIFI] render complete state=%d elapsed=%lums stack-free=%u\n", millis(),
                     static_cast<int>(renderState), millis() - renderStartedAt,
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    }
    xSemaphoreGive(renderingMutex);
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Renders the current screen content
 */
void WifiSelectionActivity::render() const {
  renderer.clearScreen();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int startY = 0;

  switch (state) {
    case WifiSelectionState::SAVED_NETWORKS:
      if (usesFileTransferLayout()) {
        renderSavedNetworks(screenWidth, screenHeight, startY);
      } else {
        renderScanning(screenWidth, screenHeight, startY);
      }
      break;
    case WifiSelectionState::SCANNING:
      if (usesFileTransferLayout()) {
        renderSavedNetworks(screenWidth, screenHeight, startY);
      } else {
        renderScanning(screenWidth, screenHeight, startY);
      }
      break;
    case WifiSelectionState::NETWORK_LIST:
      if (usesFileTransferLayout()) {
        renderSavedNetworks(screenWidth, screenHeight, startY);
      } else {
        renderNetworkList(screenWidth, screenHeight, startY);
      }
      break;
    case WifiSelectionState::CONNECTING:
      if (usesFileTransferLayout()) {
        renderConnectionCard(screenWidth, screenHeight);
      } else {
        renderConnecting(screenWidth, screenHeight, startY);
      }
      break;
    case WifiSelectionState::CONNECTED:
      if (usesFileTransferLayout()) {
        renderConnectionCard(screenWidth, screenHeight);
      } else {
        renderNetworkList(screenWidth, screenHeight, startY);
      }
      break;
    case WifiSelectionState::CONNECTION_FAILED:
      renderConnectionFailed(screenWidth, screenHeight, startY);
      break;
    case WifiSelectionState::FORGET_PROMPT:
      renderForgetPrompt(screenWidth, screenHeight, startY);
      break;
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void WifiSelectionActivity::renderSavedNetworks(const int screenWidth, const int screenHeight,
                                                const int startY) const {
  (void)startY;
  const int headerBottom = headerMode.empty()
                               ? SubPage::header(renderer, headerTitle.c_str())
                               : SubPage::header(renderer, headerTitle.c_str(), headerMode.c_str());
  const int imageX = (screenWidth - LANDING_IMAGE_WIDTH) / 2;
  const int imageY = headerBottom + LANDING_IMAGE_GAP;
  renderer.bitmap.icon(kWifiTransferSetup, imageX, imageY, LANDING_IMAGE_WIDTH, LANDING_IMAGE_HEIGHT);

  const int cardX = LANDING_CARD_PADDING;
  const int cardY = imageY + LANDING_IMAGE_HEIGHT + INITIAL_PANEL_GAP;
  const int cardWidth = screenWidth - 2 * LANDING_CARD_PADDING;
  const ButtonBounds connect = landingConnectBounds(renderer, !networks.empty());
  const ButtonBounds refresh = landingRefreshBounds(renderer, !networks.empty());
  const int panelBottom = landingPanelBottom(renderer, !networks.empty());
  const int panelHeight = std::max(1, panelBottom - cardY);
  // Match the statistics bottom section: a tall sparse dithered panel, not a
  // solid gray fill. The network rows and actions sit inside it as white cards.
  for (int py = cardY; py < panelBottom; py += 2) {
    const int offset = ((py - cardY) & 2) != 0 ? 0 : 1;
    for (int px = offset; px < screenWidth; px += 2) {
      renderer.drawPixel(px, py, true);
    }
  }

  const int listStartY = cardY + LANDING_CARD_PADDING;
  const int itemHeight = landingCardHeight(panelHeight);
  const int maxVisibleNetworks = landingVisibleRows(panelHeight);
  const int pageCount = std::max(1, (static_cast<int>(networks.size()) + maxVisibleNetworks - 1) /
                                        maxVisibleNetworks);
  const int activePage = std::max(0, std::min(networkPage, pageCount - 1));
  const int pageStart = activePage * maxVisibleNetworks;
  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
  if (state == WifiSelectionState::SCANNING) {
    BaseCarousel::renderShadow(renderer, cardX + 4, listStartY + 4, cardWidth, itemHeight,
                               HomeTheme::CarouselShadowStyle::Gray);
    renderer.rectangle.fill(cardX, listStartY, cardWidth, itemHeight, false, rounded);
    renderer.rectangle.render(cardX, listStartY, cardWidth, itemHeight, true, rounded);
    const int font = systemFontId();
    const std::string searching = renderer.text.truncate(
        font, "Searching for connections...", std::max(1, cardWidth - 2 * LANDING_CARD_PADDING));
    const int textY = listStartY + (itemHeight - renderer.text.getLineHeight(font)) / 2;
    renderer.text.centered(font, textY, searching.c_str());
  } else if (networks.empty()) {
    BaseCarousel::renderShadow(renderer, cardX + 4, listStartY + 4, cardWidth, itemHeight,
                               HomeTheme::CarouselShadowStyle::Gray);
    renderer.rectangle.fill(cardX, listStartY, cardWidth, itemHeight, false, rounded);
    renderer.rectangle.render(cardX, listStartY, cardWidth, itemHeight, true, rounded);
    const char* emptyText = state == WifiSelectionState::NETWORK_LIST ? "No networks found"
                                                                       : "No saved Wi-Fi networks";
    renderer.text.centered(systemFontId(), listStartY + itemHeight / 2, emptyText);
  } else {
    for (int displayIndex = 0; displayIndex < maxVisibleNetworks; ++displayIndex) {
      const int networkIndex = pageStart + displayIndex;
      if (networkIndex >= static_cast<int>(networks.size())) break;

      const int itemY = listStartY + displayIndex * (itemHeight + LANDING_CARD_GAP);
      const bool selected = selectedNetworkVisible && networkIndex == selectedNetworkIndex;
      const int font = systemFontId();
      const int textY = itemY + (itemHeight - renderer.text.getLineHeight(font)) / 2;
      const int itemX = cardX;
      const int itemWidth = cardWidth;
      BaseCarousel::renderShadow(renderer, itemX + 4, itemY + 4, itemWidth, itemHeight,
                                 HomeTheme::CarouselShadowStyle::Gray);
      renderer.rectangle.fill(itemX, itemY, itemWidth, itemHeight, false, rounded);
      renderer.rectangle.render(itemX, itemY, itemWidth, itemHeight, true, rounded);
      renderer.text.render(font, itemX + LANDING_CARD_PADDING, textY, networks[networkIndex].ssid.c_str(), true,
                           EpdFontFamily::REGULAR);

      const int toggleX = itemX + itemWidth - LANDING_CARD_PADDING - 10;
      const int toggleY = itemY + itemHeight / 2;
      renderer.circle.render(toggleX, toggleY, 10, true);
      renderer.circle.render(toggleX, toggleY, 7, false);
      if (selected) renderer.circle.render(toggleX, toggleY, 4, true);

    }
    Paginator::render(renderer, activePage, pageCount, screenWidth - 10, listStartY,
                      maxVisibleNetworks * itemHeight + (maxVisibleNetworks - 1) * LANDING_CARD_GAP);
  }

  Button::render(renderer, refresh, "Refresh", false, systemFontId());
  const char* label = networks.empty() ? "Search" : "Connect";
  Button::render(renderer, connect, label, !networks.empty(), systemFontId());
  mappedInput.mapLabels("« Back", label, "", "");
}

void WifiSelectionActivity::renderConnectionCard(const int screenWidth, const int screenHeight) const {
  const int headerBottom = headerMode.empty()
                               ? SubPage::header(renderer, headerTitle.c_str())
                               : SubPage::header(renderer, headerTitle.c_str(), headerMode.c_str());
  const int imageX = (screenWidth - LANDING_IMAGE_WIDTH) / 2;
  const int imageY = headerBottom + LANDING_IMAGE_GAP;
  renderer.bitmap.icon(kWifiTransferSetup, imageX, imageY, LANDING_IMAGE_WIDTH, LANDING_IMAGE_HEIGHT);

  const int panelX = 0;
  const int panelY = imageY + LANDING_IMAGE_HEIGHT + CONNECTION_PANEL_GAP;
  const int panelWidth = screenWidth;
  const ButtonBounds disconnect = connectedDisconnectBounds(renderer);
  // Match the initial Wi-Fi screen: keep a visible dithered container and a
  // finite white card above the bottom action area.
  const int panelBottom = std::max(panelY + 1, disconnect.y - LANDING_PANEL_BUTTON_GAP);
  const int panelHeight = std::max(1, panelBottom - panelY);

  // Keep the same light dither used by the Statistics bottom section.
  for (int py = panelY; py < panelBottom; py += 2) {
    const int offset = ((py - panelY) & 2) != 0 ? 0 : 1;
    for (int px = panelX + offset; px < panelX + panelWidth; px += 2) {
      renderer.drawPixel(px, py, true);
    }
  }

  const int cardX = LANDING_CARD_PADDING;
  const int cardY = panelY + LANDING_CARD_PADDING;
  const int cardWidth = screenWidth - 2 * LANDING_CARD_PADDING;
  const int bodyFont = systemFontId();
  const int labelFont = systemFontId();
  const int bodyLine = renderer.text.getLineHeight(bodyFont);
  const int connectedCardHeight = LANDING_CARD_PADDING + TRANSFER_QR_SIZE +
                                  (bodyLine * 4) + 18 + LANDING_CARD_PADDING;
  const int desiredCardHeight = state == WifiSelectionState::CONNECTING ? LIST_ITEM_HEIGHT
                                                                         : connectedCardHeight;
  const int cardHeight = std::max(1, std::min(panelHeight - LANDING_CARD_PADDING, desiredCardHeight));
  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
  BaseCarousel::renderShadow(renderer, cardX + 4, cardY + 4, cardWidth, cardHeight,
                             HomeTheme::CarouselShadowStyle::Gray);
  renderer.rectangle.fill(cardX, cardY, cardWidth, cardHeight, false, rounded);
  renderer.rectangle.render(cardX, cardY, cardWidth, cardHeight, true, rounded);

  if (state == WifiSelectionState::CONNECTING) {
    const std::string prefix = "Connecting to ";
    const int innerWidth = std::max(1, cardWidth - 2 * LANDING_CARD_PADDING);
    const int prefixWidth = renderer.text.getWidth(bodyFont, prefix.c_str(), EpdFontFamily::REGULAR);
    const int ssidWidth = std::max(1, innerWidth - prefixWidth);
    const std::string ssid = renderer.text.truncate(bodyFont, selectedSSID.c_str(), ssidWidth,
                                                    EpdFontFamily::BOLD);
    const int totalWidth = prefixWidth + renderer.text.getWidth(bodyFont, ssid.c_str(), EpdFontFamily::BOLD);
    const int textX = cardX + (cardWidth - totalWidth) / 2;
    const int textY = cardY + (cardHeight - bodyLine) / 2;
    renderer.text.render(bodyFont, textX, textY, prefix.c_str(), true, EpdFontFamily::REGULAR);
    renderer.text.render(bodyFont, textX + prefixWidth, textY, ssid.c_str(), true, EpdFontFamily::BOLD);
    mappedInput.mapLabels("« Back", "", "", "");
    return;
  }

  const std::string transferUrl = "http://inx.local/";
  const int contentX = cardX + LANDING_CARD_PADDING;
  const int contentY = cardY + LANDING_CARD_PADDING;
  const int qrX = contentX;
  const int qrY = contentY;
  drawQRCode(qrX, qrY, transferUrl);

  const int detailsX = qrX + TRANSFER_QR_SIZE + 24;
  const int detailsRight = cardX + cardWidth - LANDING_CARD_PADDING;
  const int detailsWidth = std::max(80, detailsRight - detailsX);
  const int detailFont = systemFontId();
  const int detailLine = renderer.text.getLineHeight(detailFont);
  const int statusHeight = detailLine + 8;
  const int statusWidth = renderer.text.getWidth(labelFont, "Connected", EpdFontFamily::BOLD) + 24;
  const int detailsY = contentY;
  const std::string ssidText = selectedSSID.empty() ? WiFi.SSID().c_str() : selectedSSID;
  const std::string wifiText = renderer.text.truncate(detailFont, ssidText.c_str(), detailsWidth);
  const std::string ipText = connectedIP.empty() ? "Unavailable" : connectedIP;
  const std::string ipLine = "2. " + ipText;

  renderer.rectangle.fill(detailsX, detailsY, statusWidth, statusHeight, true);
  renderer.text.render(labelFont, detailsX + 12,
                       detailsY + (statusHeight - renderer.text.getLineHeight(labelFont)) / 2,
                       "Connected", false, EpdFontFamily::BOLD);
  const int wifiY = detailsY + statusHeight + 2;
  renderer.text.render(detailFont, detailsX, wifiY, wifiText.c_str(), true,
                       EpdFontFamily::REGULAR);
  const int headingY = qrY + TRANSFER_QR_SIZE + 8;
  renderer.text.render(bodyFont, contentX, headingY, "Scan and connect", true, EpdFontFamily::BOLD);
  const int instructionY = headingY + bodyLine + 2;
  renderer.text.render(bodyFont, contentX, instructionY, "Connect to the following:", true,
                       EpdFontFamily::REGULAR);
  const int linkY = instructionY + bodyLine + 2;
  renderer.text.render(bodyFont, contentX, linkY, "1. http://inx.local", true, EpdFontFamily::REGULAR);
  renderer.text.render(bodyFont, contentX, linkY + bodyLine + 2, ipLine.c_str(), true,
                       EpdFontFamily::REGULAR);

  Button::render(renderer, disconnect, "Disconnect", true, systemFontId());
  mappedInput.mapLabels("« Back", "Disconnect", "", "");
}

void WifiSelectionActivity::drawQRCode(const int x, const int y, const std::string& data) const {
  QRCode qrcode;
  uint8_t qrcodeBytes[qrcode_getBufferSize(TRANSFER_QR_VERSION)];
  qrcode_initText(&qrcode, qrcodeBytes, TRANSFER_QR_VERSION, ECC_LOW, data.c_str());

  for (uint8_t cy = 0; cy < qrcode.size; ++cy) {
    for (uint8_t cx = 0; cx < qrcode.size; ++cx) {
      if (qrcode_getModule(&qrcode, cx, cy)) {
        renderer.rectangle.fill(x + TRANSFER_QR_PIXEL_SIZE * cx, y + TRANSFER_QR_PIXEL_SIZE * cy,
                                TRANSFER_QR_PIXEL_SIZE, TRANSFER_QR_PIXEL_SIZE, true);
      }
    }
  }
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
  renderer.text.centered(systemFontId(), centerY, "Searching for connections...");

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
    renderer.text.centered(systemFontId(), centerY - 20, "No networks found");
    renderer.text.centered(systemFontId(), centerY + 10, "Press Connect to scan again");
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

      const int textX = 20;
      const int titleY = itemY + (LIST_ITEM_HEIGHT - renderer.text.getLineHeight(systemFontId())) / 2;

      const int signalIconX = screenWidth - 60;
      const int lockIconSize = 24;
      const int lockIconGap = 12;
      const int lockIconX = signalIconX - lockIconGap - lockIconSize;
      const int savedMarkerWidth = network.hasSavedPassword ? 20 : 0;
      const int nameRight = network.isEncrypted ? lockIconX - lockIconGap
                                                : signalIconX - lockIconGap;
      const int nameWidth = std::max(1, nameRight - textX - savedMarkerWidth);
      const std::string displayName = renderer.text.truncate(systemFontId(), network.ssid.c_str(), nameWidth);
      renderer.text.render(systemFontId(), textX, titleY, displayName.c_str(), !isSelected);

      if (network.isEncrypted) {
        renderer.bitmap.icon(WifiLock, lockIconX, itemY + (LIST_ITEM_HEIGHT - lockIconSize) / 2,
                             lockIconSize, lockIconSize, BitmapRender::Orientation::None, isSelected);
      }

      drawWifiIcon(screenWidth - 60, itemY + (LIST_ITEM_HEIGHT - 28) / 2, network.rssi, isSelected);

      if (network.hasSavedPassword) {
        const int markerX = (network.isEncrypted ? lockIconX : signalIconX) - 20;
        renderer.text.render(systemFontId(), markerX, titleY, "+", !isSelected);
      }

      if (i < networks.size() - 1) {
        renderer.line.render(LIST_DIVIDER_INSET, itemY + LIST_ITEM_HEIGHT - 1, screenWidth - LIST_DIVIDER_INSET,
                             itemY + LIST_ITEM_HEIGHT - 1, true, LineRender::Style::Dotted);
      }
    }

    if (scrollOffset > 0) {
      renderer.text.render(systemFontId(), screenWidth - 15, listStartY, "^");
    }
    if (scrollOffset + maxVisibleNetworks < static_cast<int>(networks.size())) {
      renderer.text.render(systemFontId(), screenWidth - 15,
                           listStartY + maxVisibleNetworks * LIST_ITEM_HEIGHT, "v");
    }

    renderer.text.render(systemFontId(), 20, screenHeight - 90, cachedMacAddress.c_str());
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
  renderer.text.centered(systemFontId(), centerY - 50, connect.c_str());
  renderer.text.centered(systemFontId(), centerY - 20, ssidInfo.c_str());
  renderer.text.centered(systemFontId(), centerY + 20, "Please wait...");

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
  renderer.text.centered(systemFontId(), errorY - 20, connectionError.c_str());

  std::string ssidInfo = std::string(LanguageManager::translateText("Network:")) + " " + selectedSSID;
  if (ssidInfo.length() > 25) {
    ssidInfo.replace(22, ssidInfo.length() - 22, "...");
  }
  renderer.text.centered(systemFontId(), errorY + 10, ssidInfo.c_str());

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
  renderer.text.centered(systemFontId(), promptY, "Forget network and remove saved password?");

  const int buttonY = promptY + 50;
  constexpr int buttonWidth = 120;
  constexpr int buttonSpacing = 30;
  constexpr int totalWidth = buttonWidth * 2 + buttonSpacing;
  const int startX = (screenWidth - totalWidth) / 2;

  if (forgetPromptSelection == 0) {
    renderer.text.render(systemFontId(), startX, buttonY, "[Cancel]");
  } else {
    renderer.text.render(systemFontId(), startX + 4, buttonY, "Cancel");
  }

  if (forgetPromptSelection == 1) {
    renderer.text.render(systemFontId(), startX + buttonWidth + buttonSpacing, buttonY,
                         "[Forget network]");
  } else {
    renderer.text.render(systemFontId(), startX + buttonWidth + buttonSpacing + 4, buttonY,
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

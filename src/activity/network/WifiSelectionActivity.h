#pragma once

/**
 * @file WifiSelectionActivity.h
 * @brief Public interface and types for WifiSelectionActivity.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "activity/ActivityWithSubactivity.h"

/**
 * @struct WifiNetworkInfo
 * @brief Structure containing information about a detected WiFi network
 */
struct WifiNetworkInfo {
  std::string ssid;
  int32_t rssi;
  bool isEncrypted;
  bool hasSavedPassword;
};

/**
 * @enum WifiSelectionState
 * @brief Possible states of the WiFi selection process
 */
enum class WifiSelectionState {
  SAVED_NETWORKS,
  SCANNING,
  NETWORK_LIST,
  PASSWORD_ENTRY,
  CONNECTING,
  CONNECTED,
  CONNECTION_FAILED,
  FORGET_PROMPT
};

/**
 * @class WifiSelectionActivity
 * @brief Activity for scanning and connecting to WiFi networks
 *
 * Handles the complete WiFi connection flow including:
 * - Scanning for available networks
 * - Displaying sorted list of networks with signal strength
 * - Password entry for encrypted networks
 * - Connection attempt with timeout
 * - Automatic password saving after a successful connection
 * - Option to forget saved networks on failure
 *
 * The activity calls onComplete with true if connected successfully,
 * false if cancelled or connection failed.
 */
class WifiSelectionActivity final : public ActivityWithSubactivity {
 public:
  /**
   * @brief Constructs a new WifiSelectionActivity
   * @param renderer Reference to the graphics renderer
   * @param mappedInput Reference to the input manager
   * @param onComplete Callback when connection process completes (true=connected, false=cancelled/failed)
   * @param headerTitle Main header title for the service using Wi-Fi
   * @param headerMode Optional gray mode label shown beside the title
   */
  explicit WifiSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 const std::function<void(bool connected)>& onComplete,
                                 const char* headerTitle = "Wifi", const char* headerMode = "")
      : ActivityWithSubactivity("WifiSelection", renderer, mappedInput),
        onComplete(onComplete),
        headerTitle(headerTitle ? headerTitle : "Wifi"),
        headerMode(headerMode ? headerMode : "") {}

  /**
   * @brief Called when entering the activity
   */
  void onEnter() override;

  /**
   * @brief Called when exiting the activity
   */
  void onExit() override;

  /**
   * @brief Main loop function called repeatedly while activity is active
   */
  void loop() override;

  /**
   * @brief Gets the IP address after successful connection
   * @return IP address string, empty if not connected
   */
  const std::string& getConnectedIP() const { return connectedIP; }

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  TaskHandle_t scanTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  WifiSelectionState state = WifiSelectionState::SCANNING;
  int selectedNetworkIndex = 0;
  bool selectedNetworkVisible = false;
  int networkPage = 0;
  std::vector<WifiNetworkInfo> networks;
  const std::function<void(bool connected)> onComplete;
  const std::string headerTitle;
  const std::string headerMode;

  /** @brief Returns whether this caller should use the file-transfer landing UI. */
  bool usesFileTransferLayout() const { return headerTitle == "File Transfer" || headerTitle == "Download"; }

  std::string selectedSSID;
  bool selectedRequiresPassword = false;

  std::string connectedIP;
  std::string connectionError;

  std::string enteredPassword;
  bool usedSavedPassword = false;
  bool completionNotified = false;

  int forgetPromptSelection = 0;

  volatile bool scanCancelled = false;
  unsigned long scanStartedAt = 0;
  unsigned long connectionStartTime = 0;
  /**
   * @brief Static trampoline function for the display task
   * @param param Pointer to the WifiSelectionActivity instance
   */
  static void taskTrampoline(void* param);

  /** Runs the native ESP-IDF scan outside loopTask. */
  static void scanTaskTrampoline(void* param);

  /**
   * @brief Main display task loop running on separate thread
   */
  [[noreturn]] void displayTaskLoop();

  /** Scans with the ESP-IDF driver and publishes the completed network list. */
  void scanTaskLoop();

  /**
   * @brief Renders the current screen content
   */
  void render() const;

  /**
   * @brief Renders the scanning state screen
   * @param screenWidth Width of the screen
   * @param screenHeight Height of the screen
   * @param startY Starting Y coordinate for content
   */
  void renderScanning(int screenWidth, int screenHeight, int startY) const;

  /** Renders the saved-network landing page shown before an explicit scan. */
  void renderSavedNetworks(int screenWidth, int screenHeight, int startY) const;

  /** Renders the connecting/connected card on the same landing page. */
  void renderConnectionCard(int screenWidth, int screenHeight) const;

  /** Draws a QR code for a transfer URL. */
  void drawQRCode(int x, int y, const std::string& data) const;

  /**
   * @brief Renders the network list screen
   * @param screenWidth Width of the screen
   * @param screenHeight Height of the screen
   * @param startY Starting Y coordinate for content
   */
  void renderNetworkList(int screenWidth, int screenHeight, int startY) const;

  /**
   * @brief Renders the connecting state screen
   * @param screenWidth Width of the screen
   * @param screenHeight Height of the screen
   * @param startY Starting Y coordinate for content
   */
  void renderConnecting(int screenWidth, int screenHeight, int startY) const;

  /**
   * @brief Renders the connection failed screen
   * @param screenWidth Width of the screen
   * @param screenHeight Height of the screen
   * @param startY Starting Y coordinate for content
   */
  void renderConnectionFailed(int screenWidth, int screenHeight, int startY) const;

  /**
   * @brief Renders the forget network prompt screen
   * @param screenWidth Width of the screen
   * @param screenHeight Height of the screen
   * @param startY Starting Y coordinate for content
   */
  void renderForgetPrompt(int screenWidth, int screenHeight, int startY) const;

  /**
   * @brief Starts an asynchronous WiFi network scan
   */
  void startWifiScan();

  /**
   * @brief Selects a network from the list to connect to
   * @param index Index of the network to select
   */
  void selectNetwork(int index);

  /**
   * @brief Attempts to connect to the selected network
   */
  void attemptConnection();

  /** Handles touchscreen taps for the list and connection prompts. */
  bool handleTouchInput();

  /**
   * @brief Checks the status of an ongoing connection attempt
   */
  void checkConnectionStatus();

  /**
   * @brief Draws a WiFi signal strength icon
   * @param x X coordinate for the icon
   * @param y Y coordinate for the icon
   * @param rssi Signal strength in dBm
   * @param isSelected Whether the current item is selected
   */
  void drawWifiIcon(int x, int y, int32_t rssi, bool isSelected) const;

};

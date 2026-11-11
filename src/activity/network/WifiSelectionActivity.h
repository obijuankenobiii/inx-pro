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
  std::string ssid;       ///< Network name (SSID)
  int32_t rssi;           ///< Signal strength in dBm
  bool isEncrypted;       ///< Whether the network requires a password
  bool hasSavedPassword;  ///< Whether saved credentials exist for this network
};

/**
 * @enum WifiSelectionState
 * @brief Possible states of the WiFi selection process
 */
enum class WifiSelectionState {
  SAVED_NETWORKS,     ///< Landing page showing saved Wi-Fi credentials without scanning
  SCANNING,           ///< Actively scanning for available networks
  NETWORK_LIST,       ///< Displaying list of found networks
  PASSWORD_ENTRY,     ///< Password entry screen for encrypted network
  CONNECTING,         ///< Attempting to establish connection
  CONNECTED,          ///< Successfully connected to network
  CONNECTION_FAILED,  ///< Connection attempt failed
  FORGET_PROMPT       ///< Asking user whether to forget a saved network
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
  TaskHandle_t displayTaskHandle = nullptr;                 ///< Handle for display task
  TaskHandle_t scanTaskHandle = nullptr;                    ///< Handle for the native Wi-Fi scan task
  SemaphoreHandle_t renderingMutex = nullptr;               ///< Mutex for rendering synchronization
  bool updateRequired = false;                              ///< Flag indicating screen needs update
  WifiSelectionState state = WifiSelectionState::SCANNING;  ///< Current activity state
  int selectedNetworkIndex = 0;                             ///< Currently selected network index
  bool selectedNetworkVisible = false;                      ///< Only show a highlight after navigation
  int networkPage = 0;                                       ///< Current page of saved/scanned networks
  std::vector<WifiNetworkInfo> networks;                    ///< List of found networks
  const std::function<void(bool connected)> onComplete;     ///< Connection completion callback
  const std::string headerTitle;                            ///< Main title for the requesting service
  const std::string headerMode;                             ///< Optional gray mode for the requesting service

  /** @brief Returns whether this caller should use the file-transfer landing UI. */
  bool usesFileTransferLayout() const { return headerTitle == "File Transfer" || headerTitle == "Download"; }

  std::string selectedSSID;               ///< SSID of selected network
  bool selectedRequiresPassword = false;  ///< Whether selected network requires password

  std::string connectedIP;      ///< IP address after successful connection
  std::string connectionError;  ///< Error message if connection failed

  std::string enteredPassword;   ///< Password entered by user or from saved credentials
  bool usedSavedPassword = false;  ///< Whether the connection used a saved password
  bool completionNotified = false; ///< Prevents duplicate success callbacks while the connected card is shown

  int forgetPromptSelection = 0;  ///< Forget prompt selection (0=Cancel, 1=Forget)

  volatile bool scanCancelled = false;     ///< Stops a native scan before this activity is destroyed
  unsigned long scanStartedAt = 0;         ///< Timestamp used by the Wi-Fi scan diagnostics
  unsigned long connectionStartTime = 0;  ///< Start time of current connection attempt
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

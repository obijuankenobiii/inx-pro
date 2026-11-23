/**
 * @file main.cpp
 * @brief Firmware entry point, globals, and activity bootstrap.
 */

#include <Arduino.h>
#if FREEINK_CAP_BLE_HID_HOST
#include <BleKeyboardHost.h>
#endif
#include <GfxRenderer.h>
#include <ImageDisplayCache.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <SDCardManager.h>
#include <SPI.h>
#ifndef SIMULATOR
#include <esp_system.h>
#endif

#include <cstring>
#include <cstdlib>
#include <functional>
#include <new>
#include <string>
#include <tuple>
#include <type_traits>

#include "activity/network/CalibreConnectActivity.h"
#include "activity/network/HotspotActivity.h"
#include "activity/network/LocalNetworkActivity.h"
#include "activity/page/Library.h"
#include "activity/page/BookDetailsActivity.h"
#include "activity/page/Home.h"
#include "activity/page/HomeSubPage.h"
#include "activity/page/Search.h"
#include "activity/page/Settings.h"
#include "activity/page/Statistics.h"
#include "activity/page/HeatmapReport.h"
#include "activity/page/SyncActivity.h"
#include "activity/settings/StoreActivity.h"
#include "activity/settings/AppsActivity.h"
#include "activity/reader/ImageViewerActivity.h"
#include "activity/reader/ReaderActivity.h"
#include "activity/system/BootActivity.h"
#include "activity/system/SleepActivity.h"
#include "activity/util/KeyboardEntryActivity.h"
#include "activity/util/FullScreenMessageActivity.h"
#include "DeviceRuntime.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/ScreenComponents.h"
#include "system/GlobalGestureHandler.h"
#include "system/MappedInputManager.h"
#if FREEINK_CAP_BLE_HID_HOST
#include "system/BleMapping.h"
#include "system/Bluetooth.h"
#endif
#include "system/PluginManager.h"
#include "util/LibraryIndexRefresh.h"
#include "util/StringUtils.h"

#ifdef SIMULATOR
extern HalDisplay display;
extern HalGPIO gpio;
#else
HalDisplay display;
HalGPIO gpio;
#endif
MappedInputManager input(gpio);
GfxRenderer renderer(display);
GfxRenderer& render = renderer;
GlobalGestureHandler globalGestureHandler(renderer);

Activity* currentActivity = nullptr;
bool sdCardAvailable = false;
bool activityLoopRunning = false;
std::function<void()> deferredActivitySwitch;

namespace {
bool isHomeActivity(const Activity* activity) {
  return activity && std::strcmp(activity->getName(), "Home") == 0;
}

bool isBookReaderActivity(const Activity* activity) {


  return activity && std::strcmp(activity->getName(), "Reader") == 0;
}

bool isSettingsActivity(const Activity* activity) {
  return activity && std::strcmp(activity->getName(), "Settings") == 0;
}
}

unsigned long t1 = 0;
unsigned long t2 = 0;

void waitForPowerRelease();
void enterDeepSleep();
void onGoToHome();
void openSearchFromCallback(std::function<void()> returnToCaller);
void onSelectBook(const std::string& path);
void onGoToStatistics();
void onGoToHeatmapReport(HomeTheme::HeatmapView view);
void openHomeSubPage(HomeSubPage::Section section);
void openBookDetails(const std::string& bookPath, const std::string& cachePath,
                     const std::string& libraryPath);
void openDictionaryLookupKeyboard();
void openDictionaryLookup(const std::string& word);
void onGoToFileTransfer();
void onGoToSettings();
void onGoToStore();
void onGoToApps();
void onGoToLibrary(const std::string& path = "/");
void onGoToPluginLibrary();
void setupDisplayAndFonts();
void onNetworkModeSelected(NetworkMode mode);
void openReaderFromCallback(const std::string& path, std::function<void()> returnToCaller);
void openReaderFromCallback(const std::string& path, std::function<void()> returnToCaller, int spineIndex,
                            int pageNumber);
bool handleGlobalPowerRefresh();

namespace {
constexpr unsigned long kPowerDownRestartHoldMs = 2000;
constexpr unsigned long kPowerLongPressMs = 400;

const char* buttonName(const uint8_t button) {
  static constexpr const char* kNames[] = {"Back", "Confirm", "Left", "Right", "Up", "Down", "Power"};
  return button < (sizeof(kNames) / sizeof(kNames[0])) ? kNames[button] : "Unknown";
}

void logButtonEdges() {
  for (uint8_t button = HalGPIO::BTN_BACK; button <= HalGPIO::BTN_POWER; ++button) {
    if (gpio.wasPressed(button)) {
      INX_SERIAL.printf("[INPUT][BUTTON] %s(index=%u) pressed\n", buttonName(button), button);
    }
    if (gpio.wasReleased(button)) {
      INX_SERIAL.printf("[INPUT][BUTTON] %s(index=%u) released held_ms=%lu\n", buttonName(button), button,
                        gpio.getHeldTime());
    }
  }
}

#if FREEINK_CAP_BLE_HID_HOST
bool pollBluetoothInput() {
  if (!bluetoothEnabled()) return false;
  BleHid.poll();





  constexpr unsigned long kPageTurnDebounceMs = 250;
  static uint8_t lastRoutedAction = 0;
  static unsigned long lastRoutedAtMs = 0;
  const auto routePageTurn = [&](const ble_mapping::Action action, const uint32_t code, const char* source) {
    const unsigned long now = millis();
    const uint8_t actionValue = static_cast<uint8_t>(action);
    if (lastRoutedAction == actionValue && now - lastRoutedAtMs < kPageTurnDebounceMs) {
      INX_SERIAL.printf("[BLE] suppress duplicate action=%s code=%06lX source=%s age=%lu ms\n",
                        action == ble_mapping::Action::NextPage ? "NEXT" : "PREVIOUS",
                        static_cast<unsigned long>(code), source, now - lastRoutedAtMs);
      return false;
    }
    lastRoutedAction = actionValue;
    lastRoutedAtMs = now;
    input.injectButtonEdge(action == ble_mapping::Action::NextPage ? MappedInputManager::Button::PageForward
                                                                    : MappedInputManager::Button::PageBack);
    return true;
  };

  static int lastBleState = -1;
  const int bleState = (BleHid.isRunning() ? 1 : 0) | (BleHid.isConnecting() ? 2 : 0) |
                       (BleHid.isConnected() ? 4 : 0) | (BleHid.isScanning() ? 8 : 0);
  if (bleState != lastBleState) {
    lastBleState = bleState;
    INX_SERIAL.printf("[BLE] state running=%d connecting=%d connected=%d scanning=%d addr=%s name=%s\n",
                      BleHid.isRunning() ? 1 : 0, BleHid.isConnecting() ? 1 : 0, BleHid.isConnected() ? 1 : 0,
                      BleHid.isScanning() ? 1 : 0, BleHid.connectedAddr(), BleHid.connectedName());
  }

  if (!isBookReaderActivity(currentActivity)) {
    static const char* lastIgnoredActivity = nullptr;
    const char* activityName = currentActivity ? currentActivity->getName() : "(none)";
    if (activityName != lastIgnoredActivity) {
      lastIgnoredActivity = activityName;
      INX_SERIAL.printf("[BLE] input target activity=%s (page turns disabled here)\n", activityName);
    }
    if (!ble_mapping::learningActive()) {
      freeink::RawButtonEvent raw;
      while (BleHid.popRawButton(raw)) {
        INX_SERIAL.printf("[BLE] nonreader raw pressed=%d rest=%d code=%06lX report=%u byte=%u value=%02X key=%02X mods=%02X\n",
                          raw.pressed ? 1 : 0, raw.wasRest ? 1 : 0, static_cast<unsigned long>(raw.code()),
                          raw.reportId, raw.byteIndex, raw.value, raw.keycode, raw.mods);
      }
      freeink::KeyEvent key;
      while (BleHid.popKey(key)) {
        INX_SERIAL.printf("[BLE] nonreader key pressed=%d key=%02X mods=%02X special=%u ch=%02X\n",
                          key.pressed ? 1 : 0, key.keycode, key.mods, static_cast<unsigned>(key.special),
                          static_cast<unsigned char>(key.ch));
      }
    }
    return false;
  }

  bool pageTurn = false;
  bool sawRawPress = false;
  freeink::RawButtonEvent event;
  while (BleHid.popRawButton(event)) {
    if (!event.pressed || event.wasRest) continue;
    sawRawPress = true;
    INX_SERIAL.printf("[BLE] raw press code=%06lX report=%u byte=%u value=%02X key=%02X mods=%02X\n",
                      static_cast<unsigned long>(event.code()), event.reportId, event.byteIndex, event.value,
                      event.keycode, event.mods);

    ble_mapping::Action action = ble_mapping::lookup(BleHid.connectedAddr(), event.code());
    if (action == ble_mapping::Action::None && event.mods == 0) {

      switch (event.keycode) {
        case 0x4F:
        case 0x51:
        case 0x4E:
        case 0x28:
        case 0x2C:
          action = ble_mapping::Action::NextPage;
          break;
        case 0x50:
        case 0x52:
        case 0x4B:
        case 0x2A:
          action = ble_mapping::Action::PreviousPage;
          break;
        default:
          break;
      }
    }

    if (action == ble_mapping::Action::NextPage) {
      INX_SERIAL.printf("[BLE] route code=%06lX action=NEXT inject=PageForward activity=%s\n",
                        static_cast<unsigned long>(event.code()), currentActivity ? currentActivity->getName() : "(none)");
      pageTurn = routePageTurn(action, event.code(), "raw") || pageTurn;
    } else if (action == ble_mapping::Action::PreviousPage) {
      INX_SERIAL.printf("[BLE] route code=%06lX action=PREVIOUS inject=PageBack activity=%s\n",
                        static_cast<unsigned long>(event.code()), currentActivity ? currentActivity->getName() : "(none)");
      pageTurn = routePageTurn(action, event.code(), "raw") || pageTurn;
    } else {
      INX_SERIAL.printf("[BLE] route code=%06lX action=NONE activity=%s\n",
                        static_cast<unsigned long>(event.code()), currentActivity ? currentActivity->getName() : "(none)");
    }
  }




  freeink::KeyEvent key;
  while (BleHid.popKey(key)) {
    INX_SERIAL.printf("[BLE] key pressed=%d key=%02X mods=%02X special=%u ch=%02X\n", key.pressed ? 1 : 0,
                      key.keycode, key.mods, static_cast<unsigned>(key.special),
                      static_cast<unsigned char>(key.ch));
    if (sawRawPress || !key.pressed || key.mods != 0) continue;
    if (key.special == freeink::SpecialKey::Right || key.special == freeink::SpecialKey::Down ||
        key.special == freeink::SpecialKey::PageDown || key.special == freeink::SpecialKey::Enter || key.ch == ' ') {
      const uint32_t code = 0x80000000UL | static_cast<uint32_t>(key.keycode) |
                            (static_cast<uint32_t>(key.mods) << 8) |
                            (static_cast<uint32_t>(key.special) << 16);
      pageTurn = routePageTurn(ble_mapping::Action::NextPage, code, "decoded") || pageTurn;
    } else if (key.special == freeink::SpecialKey::Left || key.special == freeink::SpecialKey::Up ||
               key.special == freeink::SpecialKey::PageUp || key.special == freeink::SpecialKey::Backspace) {
      const uint32_t code = 0x80000000UL | static_cast<uint32_t>(key.keycode) |
                            (static_cast<uint32_t>(key.mods) << 8) |
                            (static_cast<uint32_t>(key.special) << 16);
      pageTurn = routePageTurn(ble_mapping::Action::PreviousPage, code, "decoded") || pageTurn;
    }
  }
  return pageTurn;
}

void startBluetoothAfterFirstFrame(Activity* activityBeforeLoop) {
  static bool startupAttempted = false;
  if (startupAttempted || activityBeforeLoop == nullptr) return;
  if (!isHomeActivity(activityBeforeLoop) && !isBookReaderActivity(activityBeforeLoop)) return;




  startupAttempted = true;
  startBluetooth();
}
#endif

}

template <typename T>
auto storeSwitchArgument(T&& value) {
  using Value = std::remove_cv_t<std::remove_reference_t<T>>;
  if constexpr (std::is_same_v<Value, GfxRenderer> || std::is_same_v<Value, MappedInputManager>) {
    return std::ref(value);
  } else {
    return std::decay_t<T>(std::forward<T>(value));
  }
}

template <typename T>
decltype(auto) useSwitchArgument(T&& value) {
  return std::forward<T>(value);
}

template <typename T>
T& useSwitchArgument(std::reference_wrapper<T> value) {
  return value.get();
}

/**
 * @brief Switches the current activity using standard heap allocation.
 *
 * A navigation callback can run from inside a child activity's loop. In that
 * case the current activity must remain alive until that loop returns; deleting
 * it inline makes its parent loop continue through freed memory.
 */
template <typename T, typename... Args>
void switchTo(Args&&... args) {
  if (activityLoopRunning) {
    auto stored = std::make_tuple(storeSwitchArgument(std::forward<Args>(args))...);
    deferredActivitySwitch = [stored = std::move(stored)]() mutable {
      std::apply(
          [](auto&&... values) {
            switchTo<T>(useSwitchArgument(std::forward<decltype(values)>(values))...);
          },
          std::move(stored));
    };
    return;
  }

  ScreenComponents::resetPageHeaderBackButton();
  DeviceRuntime::beforeActivitySwitch(display, renderer, input);
  if (currentActivity) {
    currentActivity->onExit();
#ifdef SIMULATOR



#else
    delete currentActivity;
#endif
    currentActivity = nullptr;
  }







  input.discardPendingButtonEdges();
  input.discardPendingTouchTap();
  input.discardPendingSwipe();
  input.ignoreCurrentTouch();

  currentActivity = new T(std::forward<Args>(args)...);
#ifdef SIMULATOR
  INX_SERIAL.printf("[%lu] [SIM] Activity: %s\n", millis(), currentActivity->getName());
#endif
  currentActivity->onEnter();
}

/** @brief Navigates to the new empty Home page. */
void onGoToHome() {
  switchTo<Home>(render, input);
}

void openSearchFromCallback(std::function<void()> returnToCaller) {
  switchTo<Search>(render, input, std::move(returnToCaller));
}

bool isExportedNoteImage(const std::string& path) {
  constexpr const char* root = "/Bookmarks & Annotations";
  const size_t rootLen = strlen(root);
  const bool inRoot = path.compare(0, rootLen, root) == 0 && (path.size() == rootLen || path[rootLen] == '/');
  return inRoot && (StringUtils::checkFileExtension(path, ".bmp") || StringUtils::checkFileExtension(path, ".jpg") ||
                    StringUtils::checkFileExtension(path, ".jpeg") || StringUtils::checkFileExtension(path, ".png"));
}

/**
 * @brief Opens the reader activity and returns to the library when closed.
 */
void openReaderFromCallback(const std::string& path, std::function<void()> returnToCaller) {
  openReaderFromCallback(path, std::move(returnToCaller), -1, -1);
}

void openReaderFromCallback(const std::string& path, std::function<void()> returnToCaller, const int spineIndex,
                            const int pageNumber) {
  const std::string pathCopy = path;
  INX_SERIAL.printf("[%lu] [READER-OPEN] request path=%s spine=%d page=%d\n", millis(), pathCopy.c_str(), spineIndex,
                    pageNumber);
  if (isExportedNoteImage(pathCopy)) {
    switchTo<ImageViewerActivity>(render, input, pathCopy, std::move(returnToCaller));
    return;
  }
  switchTo<ReaderActivity>(render, input, pathCopy,
                           [returnToCaller](const std::string&) {
                             if (returnToCaller) returnToCaller();
                           },
                           spineIndex, pageNumber);
}

/**
 * @brief Callback wrapper for selecting a book to read.
 */
void onSelectBook(const std::string& path) {
  openReaderFromCallback(path, [] { onGoToHome(); });
}

/**
 * @brief Navigates to the statistics activity.
 */
void onGoToStatistics() {
  if (SETTINGS.disableStatistics != 0) return;
  switchTo<Statistics>(render, input, [] { onGoToHome(); });
}

void onGoToHeatmapReport(const HomeTheme::HeatmapView view) {
  switchTo<HeatmapReport>(render, input, view, [] { onGoToHome(); });
}

void openHomeSubPage(const HomeSubPage::Section section) {
  switchTo<HomeSubPage>(render, input, section, [] { onGoToHome(); });
}

void openHomeBookDetails(const std::string& bookPath, const std::string& cachePath) {
  switchTo<BookDetailsActivity>(render, input, bookPath, cachePath, [] { onGoToHome(); });
}

void openBookDetails(const std::string& bookPath, const std::string& cachePath,
                     const std::string& libraryPath) {
  switchTo<BookDetailsActivity>(render, input, bookPath, cachePath,
                                [libraryPath] { onGoToLibrary(libraryPath); });
}

void openDictionaryLookup(const std::string& word) {
  switchTo<HomeSubPage>(render, input, HomeSubPage::Section::Dictionary, [] { onGoToHome(); }, word);
}

void openDictionaryLookupKeyboard() {
  switchTo<KeyboardEntryActivity>(
      render, input, "Look up", "", 10, 64, false,
      [](const std::string& word) {
        if (word.empty()) {
          openHomeSubPage(HomeSubPage::Section::Dictionary);
        } else {
          openDictionaryLookup(word);
        }
      },
      [] { openHomeSubPage(HomeSubPage::Section::Dictionary); });
}

/**
 * @brief Handles network mode selection and navigates to appropriate activity.
 */
void onNetworkModeSelected(NetworkMode mode) {
  switch (mode) {
    case NetworkMode::JOIN_NETWORK:
      switchTo<LocalNetworkActivity>(render, input, onGoToFileTransfer);
      break;
    case NetworkMode::CONNECT_CALIBRE:
      switchTo<CalibreConnectActivity>(render, input, onGoToFileTransfer);
      break;
    case NetworkMode::CREATE_HOTSPOT:
      switchTo<HotspotActivity>(render, input, onGoToFileTransfer);
      break;
  }
}

/**
 * @brief Navigates to the file transfer/sync activity.
 */
void onGoToFileTransfer() {
  INX_SERIAL.printf("[NAV] Device Management\n");
  switchTo<SyncActivity>(render, input, onNetworkModeSelected);
}

/**
 * @brief Navigates to the settings activity.
 */
void onGoToSettings() {
  INX_SERIAL.printf("[NAV] Settings\n");
  switchTo<Settings>(render, input);
}

/** @brief Opens the Home drawer's software store page. */
void onGoToStore() {
  switchTo<StoreActivity>(render, input, [] { onGoToHome(); });
}

/** @brief Opens the application shortcuts page. */
void onGoToApps() {
  switchTo<AppsActivity>(render, input, [] { onGoToHome(); });
}

/**
 * @brief Navigates to the library activity.
 */
void onGoToLibrary(const std::string& path) {
  INX_SERIAL.printf("[NAV] Library path=%s\n", path.c_str());
  switchTo<Library>(render, input, path);
}

void onGoToPluginLibrary() {
  PluginManager::LibraryMenuLink link;
  if (!PluginManager::findLibraryMenuPlugin(link)) {
    onGoToLibrary("/");
    return;
  }
  switchTo<Library>(render, input, "/", std::move(link));
}

void waitForPowerRelease() {
  constexpr uint8_t kWakeDebounceSamples = 3;
  bool powerPressed = false;
  for (uint8_t sample = 0; sample < kWakeDebounceSamples; ++sample) {
    gpio.update();
    powerPressed = gpio.isPressed(HalGPIO::BTN_POWER);
    if (powerPressed) break;
    delay(10);
  }

  while (powerPressed) {
    delay(10);
    gpio.update();
    powerPressed = gpio.isPressed(HalGPIO::BTN_POWER);
  }
}

void enterDeepSleep() {
#if FREEINK_CAP_BLE_HID_HOST
  stopBluetooth();
#endif
  switchTo<SleepActivity>(render, input);
  display.deepSleep();
  SdMan.shutdown();
  DeviceRuntime::startDeepSleep(gpio);
}

void setupDisplayAndFonts() {
  display.begin();
  render.begin();
  FontManager::initialize(render);
}

bool handleGlobalPowerRefresh() {
  if (!currentActivity || !currentActivity->allowGlobalPowerRefresh()) {
    return false;
  }
  const bool readerPowerAction = currentActivity->handlesReaderPowerButton() &&
                                 READER_SETTINGS.btnPowerShortAction != SystemSetting::BTN_ACTION_NONE;
  if (readerPowerAction) {
    return false;
  }
  if (SETTINGS.shortPwrBtn != SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {
    return false;
  }
  if (!input.wasReleased(MappedInputManager::Button::Power)) {
    return false;
  }

  DeviceRuntime::refreshAfterPowerButton(display, renderer);
  return true;
}

/**
 * @brief Set up application.
 */
void setup() {
  t1 = millis();
  gpio.begin();
  DeviceRuntime::initialize(gpio);

  sdCardAvailable = SdMan.begin();

  setupDisplayAndFonts();

  switch (gpio.getWakeupReason()) {
    case HalGPIO::WakeupReason::AfterUSBPower:
      display.deepSleep();
      SdMan.shutdown();
      gpio.startDeepSleep();
      break;
    default:
      break;
  }

  switchTo<BootActivity>(render, input);
  waitForPowerRelease();
}

/**
 * @brief All activity loop.
 */
void loop() {
  gpio.update();
  logButtonEdges();
#if FREEINK_CAP_BLE_HID_HOST
  const bool bleActivity = pollBluetoothInput();
#else
  constexpr bool bleActivity = false;
#endif
#if FREEINK_DEVICE_X4PRO
  if (SETTINGS.x4proHomeButton == SystemSetting::X4PRO_HOME_BUTTON_HOME &&
      gpio.touchSwipe() == HalGPIO::TouchSwipe::None && gpio.wasHomeKeyTapped()) {
    INX_SERIAL.printf("[%lu] [INPUT][HOME] X4 Pro Home button -> Home\n", millis());
    onGoToHome();
    delay(10);
    return;
  }
#endif
  const bool powerHeld = gpio.isPressed(HalGPIO::BTN_POWER);
  const bool downHeld = gpio.isPressed(HalGPIO::BTN_DOWN);
  static bool powerDownComboTracking = false;
  static unsigned long powerDownComboStartedAt = 0;

  const bool powerDownComboHeld = powerHeld && downHeld;
  if (powerDownComboHeld) {
    if (!powerDownComboTracking) {
      powerDownComboTracking = true;
      powerDownComboStartedAt = millis();
      INX_SERIAL.println("[MAIN] power+down restart chord started");
    }
    if (millis() - powerDownComboStartedAt >= kPowerDownRestartHoldMs) {
      INX_SERIAL.println("[MAIN] power+down restart chord triggered");
#ifndef SIMULATOR
      esp_restart();
#else
      powerDownComboTracking = false;
#endif
      return;
    }
    delay(10);
    return;
  }
  powerDownComboTracking = false;

  LibraryIndexRefresh::render(renderer);
  LibraryIndexRefresh::finish(renderer, currentActivity);
  static unsigned long lastActivityTime = millis();

  const bool inputActivity = bleActivity || gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.wasTouchActivity();
  if (inputActivity || (currentActivity && currentActivity->preventAutoSleep())) {
    lastActivityTime = millis();
  }

  if (SETTINGS.sleepTimeout != SystemSetting::SLEEP_NEVER &&
      millis() - lastActivityTime >= SETTINGS.getSleepTimeoutMs()) {
    enterDeepSleep();
    return;
  }

  const bool readerPowerAction = currentActivity && currentActivity->handlesReaderPowerButton() &&
                                 READER_SETTINGS.btnPowerShortAction != SystemSetting::BTN_ACTION_NONE;
  const unsigned long powerSleepThreshold = readerPowerAction ? kPowerLongPressMs : SETTINGS.getPowerButtonDuration();
  if (powerHeld && gpio.getHeldTime() > powerSleepThreshold) {
    enterDeepSleep();
    return;
  }

  if (handleGlobalPowerRefresh()) {
    delay(10);
    return;
  }

  if (LibraryIndexRefresh::isRunning()) {
    delay(10);
    return;
  }



  const bool allowGlobalExitGesture = !isHomeActivity(currentActivity) && !isBookReaderActivity(currentActivity) &&
                                      !isSettingsActivity(currentActivity);


  if (globalGestureHandler.handleInput(input, allowGlobalExitGesture, !isBookReaderActivity(currentActivity),
                                       [] { input.injectButtonEdge(MappedInputManager::Button::Back); },
                                       [] { onGoToHome(); })) {
    delay(10);
    return;
  }

  if (currentActivity) {
    Activity* activityBeforeLoop = currentActivity;
    if (ScreenComponents::pageHeaderBackButtonVisible()) {
      input.consumeHeaderBackTap(renderer);
    }
    activityLoopRunning = true;
    currentActivity->loop();
    activityLoopRunning = false;
#if FREEINK_CAP_BLE_HID_HOST
    startBluetoothAfterFirstFrame(activityBeforeLoop);
#endif
  }



  DeviceRuntime::poll(gpio, enterDeepSleep);

#if FREEINK_DEVICE_MURPHY_M4
  display.serviceIdle();
#endif

  if (deferredActivitySwitch) {
    auto action = std::move(deferredActivitySwitch);
    deferredActivitySwitch = nullptr;
    action();
    return;
  }

  if (!inputActivity) {
    ImageDisplayCache::flushDeferredWrites();
  }

  if (currentActivity && currentActivity->skipLoopDelay()) {
    delay(1);
  } else {
    delay(10);
  }
}

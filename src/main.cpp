/**
 * @file main.cpp
 * @brief Firmware entry point, globals, and activity bootstrap.
 */

#include <Arduino.h>
#include <GfxRenderer.h>
#include <ImageDisplayCache.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <SDCardManager.h>
#include <SPI.h>
#if FREEINK_DEVICE_X4PRO
#include <XteinkDetect.h>
#endif
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

#include "activity/OpdsServerListActivity.h"
#include "activity/network/CalibreConnectActivity.h"
#include "activity/network/HotspotActivity.h"
#include "activity/network/LocalNetworkActivity.h"
#include "activity/page/Library.h"
#include "activity/page/Home.h"
#include "activity/page/HomeSubPage.h"
#include "activity/page/Search.h"
#include "activity/page/Settings.h"
#include "activity/page/Statistics.h"
#include "activity/page/SyncActivity.h"
#include "activity/reader/ImageViewerActivity.h"
#include "activity/reader/ReaderActivity.h"
#include "activity/system/BootActivity.h"
#include "activity/system/SleepActivity.h"
#include "activity/util/KeyboardEntryActivity.h"
#include "activity/util/FullScreenMessageActivity.h"
#include "state/OpdsServerStore.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Frontlight.h"
#if FREEINK_DEVICE_X4PRO
#include "system/FrontlightPreferences.h"
#endif
#include "system/Fonts.h"
#include "system/ScreenComponents.h"
#include "system/MappedInputManager.h"
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

#if FREEINK_DEVICE_X4PRO
FrontlightManager frontlight;
#endif

Activity* currentActivity = nullptr;
bool sdCardAvailable = false;
bool activityLoopRunning = false;
std::function<void()> deferredActivitySwitch;

unsigned long t1 = 0;
unsigned long t2 = 0;

void verifyPowerButtonDuration();
void waitForPowerRelease();
void enterDeepSleep();
void onGoToHome();
void openSearchFromCallback(std::function<void()> returnToCaller);
void onSelectBook(const std::string& path);
void onGoToStatistics();
void openHomeSubPage(HomeSubPage::Section section);
void openDictionaryLookupKeyboard();
void openDictionaryLookup(const std::string& word);
void onGoToFileTransfer();
void onGoToSettings();
void onGoToLibrary(const std::string& path = "/");
void setupDisplayAndFonts();
void onNetworkModeSelected(NetworkMode mode);
void openReaderFromCallback(const std::string& path, std::function<void()> returnToCaller);
void openReaderFromCallback(const std::string& path, std::function<void()> returnToCaller, int spineIndex,
                            int pageNumber);
bool handleGlobalPowerRefresh();

namespace {
constexpr unsigned long kPowerDownRestartHoldMs = 2000;
constexpr unsigned long kPowerLongPressMs = 400;
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
  if (currentActivity) {
    currentActivity->onExit();
    delete currentActivity;
    currentActivity = nullptr;
  }

#if FREEINK_DEVICE_X4PRO
  // Input hygiene across EVERY activity transition, not just the reader's.
  //
  // A page refresh here takes ~0.5 s, and a full one ~1.3 s, so a finger is routinely still
  // down when the next activity is constructed. Three things can survive the switch and be
  // replayed on a screen that never saw the gesture start:
  //   - a tap already buffered by the outgoing activity's hit-testing
  //   - a swipe latched for the remainder of this update cycle
  //   - a touch still physically held, whose release synthesizes a tap later
  // The last one is what produced "swipe up lands on Settings": the release arrived after
  // the switch and was hit-tested against the new page's bottom nav.
  //
  // Doing this centrally means every page is covered; individual activities do not each
  // have to remember.
  input.discardPendingTouchTap();
  input.discardPendingSwipe();
  input.ignoreCurrentTouch();
#endif

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
  // Defensive copy: `path` is typically a reference into the calling activity's own state (e.g.
  // the previous library activity's currentPageItems), but switchTo() deletes that activity before this function's
  // arguments are used to construct the new one - passing `path` itself through would dangle.
  const std::string pathCopy = path;
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
  switchTo<Statistics>(render, input, [] { onGoToHome(); });
}

void openHomeSubPage(const HomeSubPage::Section section) {
  switchTo<HomeSubPage>(render, input, section, [] { onGoToHome(); });
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
    case NetworkMode::OPDS_BROWSER:
      switchTo<OpdsServerListActivity>(render, input, onGoToFileTransfer);
      break;
  }
}

/**
 * @brief Navigates to the file transfer/sync activity.
 */
void onGoToFileTransfer() {
  INX_SERIAL.printf("[STICKY][NAV] Device Management\n");
  switchTo<SyncActivity>(render, input, onNetworkModeSelected);
}

/**
 * @brief Navigates to the settings activity.
 */
void onGoToSettings() {
  INX_SERIAL.printf("[STICKY][NAV] Settings\n");
  switchTo<Settings>(render, input);
}

/**
 * @brief Navigates to the library activity.
 */
void onGoToLibrary(const std::string& path) {
  INX_SERIAL.printf("[STICKY][NAV] Library path=%s\n", path.c_str());
  switchTo<Library>(render, input, path);
}

/**
 * @brief Set up application.
 */
void verifyPowerButtonDuration() {
  // A short power press must be sufficient both to sleep while awake and to
  // wake from deep sleep. Accept the wake immediately; waitForPowerRelease()
  // below still prevents the held wake press from leaking into the first page.
  if (SETTINGS.shortPressPowerButton || SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::SLEEP) return;
  const auto start = millis();
  bool abort = false;
  gpio.update();
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    delay(10);
    gpio.update();
  }

  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() < SETTINGS.getPowerButtonDuration()) {
      delay(10);
      gpio.update();
    }
    abort = gpio.getHeldTime() < SETTINGS.getPowerButtonDuration();
  } else {
    abort = true;
  }

  if (abort) gpio.startDeepSleep();
}

void waitForPowerRelease() {
  // The wake press may still be inside InputManager's debounce window when setup
  // finishes. Do not let one idle sample make the main loop treat that same held
  // press as a new short-press sleep command.
  constexpr uint8_t kWakeDebounceSamples = 3;
  bool powerPressed = false;
  for (uint8_t sample = 0; sample < kWakeDebounceSamples; ++sample) {
    gpio.update();
    powerPressed = gpio.isPressed(HalGPIO::BTN_POWER);
    if (powerPressed) break;
    delay(10);
  }

  // Consume the original wake press through its actual release. A release edge
  // is generated before setup returns, so it cannot leak into BootActivity or
  // the recently-opened reader.
  while (powerPressed) {
    delay(10);
    gpio.update();
    powerPressed = gpio.isPressed(HalGPIO::BTN_POWER);
  }
}

void enterDeepSleep() {
  switchTo<SleepActivity>(render, input);
  display.deepSleep();
  gpio.startDeepSleep();
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
  // EpubReader dispatches its own ReaderSetting power action on release. Do not consume the
  // same release here as the device-level page refresh first.
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

  // Refresh the framebuffer currently on screen. On dual-buffer devices the
  // inactive buffer can still contain the previous page after a swap.
  renderer.syncWriteBufferFromActive();
  renderer.displayBuffer(HalDisplay::MANUAL_REFRESH);
  return true;
}

/**
 * @brief Set up application.
 */
void setup() {
  t1 = millis();
  gpio.begin();

#if FREEINK_DEVICE_X4PRO
  // X4 Pro batches can carry SSD1677, UC8179, or UC8279 panels. Resolve the
  // controller before FreeInkDisplay::begin() selects and initializes a driver.
  freeink::applyXteinkDisplayController();
  frontlight.begin();
#endif

  sdCardAvailable = SdMan.begin();


  setupDisplayAndFonts();

  if (gpio.isUsbConnected()) {
    INX_SERIAL.begin(115200);
    unsigned long start = millis();
    while (!INX_SERIAL && (millis() - start) < 3000) delay(10);
  }

  if (sdCardAvailable) {
    SETTINGS.loadFromFile();
    renderer.setDarkMode(SETTINGS.darkMode != 0);
    READER_SETTINGS.loadFromFile();
    OPDS_STORE.loadOrMigrate({"Default", SETTINGS.opdsServerUrl, SETTINGS.opdsUsername, SETTINGS.opdsPassword});
#if FREEINK_DEVICE_X4PRO
    frontlight_preferences::Settings lightSettings;
    if (frontlight_preferences::load(lightSettings)) {
      frontlight.setColorTemperature(lightSettings.warmPercent);
      frontlight.setBrightness(lightSettings.brightness);
      if (lightSettings.enabled == 0) frontlight.off();
    }
#endif
  }
  switch (gpio.getWakeupReason()) {
    case HalGPIO::WakeupReason::PowerButton:
      verifyPowerButtonDuration();
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
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
  const bool powerPressed = gpio.wasPressed(HalGPIO::BTN_POWER);
  const bool powerReleased = gpio.wasReleased(HalGPIO::BTN_POWER);
  const bool powerHeld = gpio.isPressed(HalGPIO::BTN_POWER);
  const bool downHeld = gpio.isPressed(HalGPIO::BTN_DOWN);
  static bool powerDownComboTracking = false;
  static unsigned long powerDownComboStartedAt = 0;

  // Fixed hardware recovery chord: hold Power + physical Down for two seconds.
  // Keep it ahead of normal power handling so the same hold cannot enter sleep.
  const bool powerDownComboHeld = powerHeld && downHeld;
  if (powerDownComboHeld) {
    if (!powerDownComboTracking) {
      powerDownComboTracking = true;
      powerDownComboStartedAt = millis();
      INX_SERIAL.println("[STICKY][MAIN] power+down restart chord started");
    }
    if (millis() - powerDownComboStartedAt >= kPowerDownRestartHoldMs) {
      INX_SERIAL.println("[STICKY][MAIN] power+down restart chord triggered");
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

  if (powerPressed || powerReleased) {
    INX_SERIAL.printf("[STICKY][MAIN] power pressed=%d released=%d activity=%s\n", powerPressed ? 1 : 0,
                   powerReleased ? 1 : 0, currentActivity ? currentActivity->getName() : "none");
  }

  LibraryIndexRefresh::render(renderer);
  LibraryIndexRefresh::finish(renderer, currentActivity);
  static unsigned long lastActivityTime = millis();

  const bool inputActivity = gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.wasTouchActivity();
  if (inputActivity || (currentActivity && currentActivity->preventAutoSleep())) {
    lastActivityTime = millis();
  }

  if (millis() - lastActivityTime >= SETTINGS.getSleepTimeoutMs()) {
    enterDeepSleep();
    return;
  }

  const bool readerPowerAction = currentActivity && currentActivity->handlesReaderPowerButton() &&
                                 READER_SETTINGS.btnPowerShortAction != SystemSetting::BTN_ACTION_NONE;
  // A reader short-press mapping must not disable the hardware long-press
  // safety action. Give the mapped short action time to fire on release, then
  // always sleep when Power remains held.
  const unsigned long powerSleepThreshold = readerPowerAction ? kPowerLongPressMs : SETTINGS.getPowerButtonDuration();
  if (powerHeld && gpio.getHeldTime() > powerSleepThreshold) {
    enterDeepSleep();
    return;
  }

  if (handleGlobalPowerRefresh()) {
    delay(10);
    return;
  }

  // The global status popup blocks page input until the index task completes.
  if (LibraryIndexRefresh::isRunning()) {
    delay(10);
    return;
  }

  if (currentActivity) {
    // The shared page header consumes this touch before the activity loop. Ordinary taps are
    // buffered by MappedInputManager and remain available to the activity unchanged.
    if (ScreenComponents::pageHeaderBackButtonVisible()) {
      input.consumeHeaderBackTap(renderer);
    }
    activityLoopRunning = true;
    currentActivity->loop();
    activityLoopRunning = false;
  }

  if (deferredActivitySwitch) {
    auto action = std::move(deferredActivitySwitch);
    deferredActivitySwitch = nullptr;
    action();
    return;
  }

  // Cache pixels were captured during image rendering and are already in the
  // bounded PSRAM front cache. Persist one plane only after the activity loop
  // has completed and only on an idle-input frame, avoiding concurrent SD/SPI
  // transactions with metadata, ZIP, or display work.
  if (!inputActivity) {
    ImageDisplayCache::flushDeferredWrites();
  }

  if (currentActivity && currentActivity->skipLoopDelay()) {
    yield();
  } else {
    delay(10);
  }
}

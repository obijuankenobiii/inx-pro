/**
 * @file BootActivity.cpp
 * @brief Definitions for BootActivity.
 */

#include "BootActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <functional>

#include "KOReaderCredentialStore.h"
#include "images/CorgiWhite.h"
#include "state/OpdsServerStore.h"
#include "state/RecentBooks.h"
#include "state/ReaderSetting.h"
#include "state/Session.h"
#include "state/SystemSetting.h"
#if FREEINK_DEVICE_X4PRO
#include "system/Frontlight.h"
#include "system/FrontlightPreferences.h"
#endif

extern void onGoToHome();
extern void openReaderFromCallback(const std::string& path, std::function<void()> returnToCaller);
extern bool sdCardAvailable;
extern HalDisplay display;
extern HalGPIO gpio;
extern MappedInputManager mappedInputManager;
extern GfxRenderer renderer;
extern Activity* currentActivity;

BootActivity::BootActivity(GfxRenderer& renderer, MappedInputManager& inputManager)
    : Activity("BootActivity", renderer, inputManager) {}

/**
 * @brief Initializes the boot activity when it becomes active.
 */
void BootActivity::onEnter() {
  Activity::onEnter();

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

  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();

  if (SdMan.ready() && SdMan.exists(KOReaderCredentialStore::SYSTEM_SETTINGS_PATH)) {
    (void)KOREADER_STORE.loadFromFile();
  }

  bootComplete = true;
}

/**
 * @brief Main update loop for the boot activity.
 */
void BootActivity::loop() {
  if (bootComplete) {
    if (SETTINGS.bootSetting == SystemSetting::RECENT_PAGE && !APP_STATE.lastRead.empty()) {
      openReaderFromCallback(APP_STATE.lastRead, [] { onGoToHome(); });
      return;
    }
    onGoToHome();
    return;
  }
}

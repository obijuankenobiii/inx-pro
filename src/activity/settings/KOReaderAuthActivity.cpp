/**
 * @file KOReaderAuthActivity.cpp
 * @brief Definitions for KOReaderAuthActivity.
 */

#include "KOReaderAuthActivity.h"

#include <GfxRenderer.h>
#include <WiFi.h>

#include "activity/page/SubPage.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncClient.h"
#include "activity/network/WifiSelectionActivity.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

void KOReaderAuthActivity::taskTrampoline(void* param) {
  auto* self = static_cast<KOReaderAuthActivity*>(param);
  self->displayTaskLoop();
}

void KOReaderAuthActivity::onWifiSelectionComplete(const bool success) {
  exitActivity();

  if (!success) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = FAILED;
    errorMessage = "WiFi connection failed";
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = AUTHENTICATING;
  statusMessage = mode == Mode::SIGN_UP ? "Creating account..." : "Authenticating...";
  xSemaphoreGive(renderingMutex);
  updateRequired = true;

  performAuthentication();
}

void KOReaderAuthActivity::performAuthentication() {
  const auto result = mode == Mode::SIGN_UP ? KOReaderSyncClient::createUser() : KOReaderSyncClient::authenticate();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (result == KOReaderSyncClient::OK) {
    state = SUCCESS;
    statusMessage = mode == Mode::SIGN_UP ? "Account created!" : "Successfully authenticated!";
  } else {
    state = FAILED;
    errorMessage = result == KOReaderSyncClient::USER_EXISTS ? "Username is already registered"
                                                              : KOReaderSyncClient::errorString(result);
  }
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
}

void KOReaderAuthActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  xTaskCreate(&KOReaderAuthActivity::taskTrampoline, "KOAuthTask", 4096, this, 1, &displayTaskHandle);

  WiFi.mode(WIFI_STA);

  if (WiFi.status() == WL_CONNECTED) {
    state = AUTHENTICATING;
    statusMessage = mode == Mode::SIGN_UP ? "Creating account..." : "Authenticating...";
    updateRequired = true;

    xTaskCreate(
        [](void* param) {
          auto* self = static_cast<KOReaderAuthActivity*>(param);
          self->performAuthentication();
          vTaskDelete(nullptr);
        },
        "AuthTask", 4096, this, 1, nullptr);
    return;
  }

  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
}

void KOReaderAuthActivity::onExit() {
  ActivityWithSubactivity::onExit();

  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void KOReaderAuthActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void KOReaderAuthActivity::render() {
  if (subActivity) {
    return;
  }

  renderer.clearScreen();
  SubPage::header(renderer, mode == Mode::SIGN_UP ? "KOReader Sign Up" : "KOReader Auth");

  if (state == AUTHENTICATING) {
    renderer.text.centered(systemFontId(), 300, statusMessage.c_str(), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    renderer.text.centered(systemFontId(), 280, mode == Mode::SIGN_UP ? "Account created!" : "Success!", true,
                           EpdFontFamily::BOLD);
    renderer.text.centered(systemFontId(), 320, "KOReader sync is ready to use");

    const auto labels = mappedInput.mapLabels("Done", "", "", "");
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.text.centered(systemFontId(), 280, mode == Mode::SIGN_UP ? "Sign-up failed" : "Authentication Failed",
                           true, EpdFontFamily::BOLD);
    renderer.text.centered(systemFontId(), 320, errorMessage.c_str());

    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    renderer.displayBuffer();
    return;
  }
}

void KOReaderAuthActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (SubPage::closeInput(renderer, mappedInput, onComplete)) return;

  if ((state == SUCCESS || state == FAILED) && mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      onComplete();
      return;
    }
  }

  if (state == SUCCESS || state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      onComplete();
    }
  }
}

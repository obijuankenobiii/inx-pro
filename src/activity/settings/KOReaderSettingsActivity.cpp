/**
 * @file KOReaderSettingsActivity.cpp
 * @brief Definitions for KOReaderSettingsActivity.
 */

#include "KOReaderSettingsActivity.h"
#include "system/UiLayout.h"

#include <GfxRenderer.h>

#include <cstring>

#include "activity/page/SubPage.h"
#include "KOReaderAuthActivity.h"
#include "KOReaderCredentialStore.h"
#include "activity/util/KeyboardEntryActivity.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

constexpr int LIST_ITEM_HEIGHT = Page::LIST_ITEM_HEIGHT;

namespace {
constexpr int MENU_ITEMS = 6;
const char* menuNames[MENU_ITEMS] = {"Username", "Password", "Sync Server URL", "Document Matching", "Sign Up",
                                     "Authenticate"};
}  // namespace

void KOReaderSettingsActivity::taskTrampoline(void* param) {
  auto* self = static_cast<KOReaderSettingsActivity*>(param);
  self->displayTaskLoop();
}

void KOReaderSettingsActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  selectedIndex = 0;
  selectedVisible = false;
  updateRequired = true;

  xTaskCreate(&KOReaderSettingsActivity::taskTrampoline, "KOReaderSettingsTask", 4096, this, 1, &displayTaskHandle);
}

void KOReaderSettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void KOReaderSettingsActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (SubPage::closeInput(renderer, mappedInput, onBack)) return;

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
      // Keep the hit-test aligned with SubPage::header(). Its top padding is
      // device-specific, while the old fixed UiLayout value was not.
      const int bodyTop = (FREEINK_DEVICE_X4PRO ? 20 : 10) + 40 + 20;
      if (tapY >= bodyTop && tapY < bodyTop + MENU_ITEMS * LIST_ITEM_HEIGHT) {
        selectedIndex = (tapY - bodyTop) / LIST_ITEM_HEIGHT;
        handleSelection();
        return;
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up) || mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    selectedIndex = (selectedIndex + MENU_ITEMS - 1) % MENU_ITEMS;
    selectedVisible = true;
    updateRequired = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) || mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    selectedIndex = (selectedIndex + 1) % MENU_ITEMS;
    selectedVisible = true;
    updateRequired = true;
  }
}

void KOReaderSettingsActivity::handleSelection() {
  xSemaphoreTake(renderingMutex, portMAX_DELAY);

  if (selectedIndex == 0) {
    exitActivity();
    enterNewActivity(new KeyboardEntryActivity(
        renderer, mappedInput, "KOReader Username", KOREADER_STORE.getUsername(), 10, 64, false,
        [this](const std::string& username) {
          KOREADER_STORE.setCredentials(username, KOREADER_STORE.getPassword());
          KOREADER_STORE.saveToFile();
          exitActivity();
          updateRequired = true;
        },
        [this]() {
          exitActivity();
          updateRequired = true;
        }));
  } else if (selectedIndex == 1) {
    exitActivity();
    enterNewActivity(new KeyboardEntryActivity(
        renderer, mappedInput, "KOReader Password", KOREADER_STORE.getPassword(), 10, 64, true,
        [this](const std::string& password) {
          KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), password);
          KOREADER_STORE.saveToFile();
          exitActivity();
          updateRequired = true;
        },
        [this]() {
          exitActivity();
          updateRequired = true;
        }));
  } else if (selectedIndex == 2) {
    const std::string currentUrl = KOREADER_STORE.getServerUrl();
    const std::string prefillUrl = currentUrl.empty() ? "https://" : currentUrl;
    exitActivity();
    enterNewActivity(new KeyboardEntryActivity(
        renderer, mappedInput, "Sync Server URL", prefillUrl, 10, 128, false,
        [this](const std::string& url) {
          const std::string urlToSave = (url == "https://" || url == "http://") ? "" : url;
          KOREADER_STORE.setServerUrl(urlToSave);
          KOREADER_STORE.saveToFile();
          exitActivity();
          updateRequired = true;
        },
        [this]() {
          exitActivity();
          updateRequired = true;
        }));
  } else if (selectedIndex == 3) {
    const auto current = KOREADER_STORE.getMatchMethod();
    const auto newMethod =
        (current == DocumentMatchMethod::FILENAME) ? DocumentMatchMethod::BINARY : DocumentMatchMethod::FILENAME;
    KOREADER_STORE.setMatchMethod(newMethod);
    KOREADER_STORE.saveToFile();
    updateRequired = true;
  } else if (selectedIndex == 4 || selectedIndex == 5) {
    if (!KOREADER_STORE.hasCredentials()) {
      xSemaphoreGive(renderingMutex);
      return;
    }
    exitActivity();
    const auto mode = selectedIndex == 4 ? KOReaderAuthActivity::Mode::SIGN_UP
                                         : KOReaderAuthActivity::Mode::AUTHENTICATE;
    enterNewActivity(new KOReaderAuthActivity(renderer, mappedInput,
                                               [this] {
                                                 exitActivity();
                                                 updateRequired = true;
                                               },
                                               mode));
  }

  xSemaphoreGive(renderingMutex);
}

void KOReaderSettingsActivity::displayTaskLoop() {
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

void KOReaderSettingsActivity::render() {
  const auto screenWidth = renderer.getScreenWidth();
  const auto screenHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  const int dividerY = SubPage::header(renderer, "KOReader");

  int startY = dividerY;
  int visibleAreaHeight = screenHeight - startY - 60;

  for (int i = 0; i < MENU_ITEMS; i++) {
    if (i * LIST_ITEM_HEIGHT < visibleAreaHeight) {
      int itemY = startY + (i * LIST_ITEM_HEIGHT);
      bool isSelected = selectedVisible && (i == selectedIndex);

      if (isSelected) {
        renderer.rectangle.fill(0, itemY, screenWidth, LIST_ITEM_HEIGHT, static_cast<int>(GfxRenderer::FillTone::Ink));
      }

      int textY = itemY + (LIST_ITEM_HEIGHT - renderer.text.getLineHeight(systemFontId())) / 2;

      renderer.text.render(systemFontId(), 20, textY, menuNames[i], !isSelected);

      const char* status = "";
      if (i == 0) {
        status = KOREADER_STORE.getUsername().empty() ? "Not Set" : "Set";
      } else if (i == 1) {
        status = KOREADER_STORE.getPassword().empty() ? "Not Set" : "Set";
      } else if (i == 2) {
        status = KOREADER_STORE.getServerUrl().empty() ? "Default" : "Custom";
      } else if (i == 3) {
        status = KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME ? "Filename" : "Binary";
      } else if (i == 4 || i == 5) {
        status = KOREADER_STORE.hasCredentials() ? "" : "Set credentials first";
      }

      if (strlen(status) > 0) {
        int statusWidth = renderer.text.getWidth(systemFontId(), status);
        renderer.text.render(systemFontId(), screenWidth - statusWidth - 40, textY, status,
                             !isSelected);
      }

      if (i != 3) {
        renderer.text.render(systemFontId(), screenWidth - 25, textY, "›", !isSelected);
      }

      if (i + 1 < MENU_ITEMS) {
        renderer.line.render(0, itemY + LIST_ITEM_HEIGHT - 1, screenWidth, itemY + LIST_ITEM_HEIGHT - 1, true,
                             LineRender::Style::Dotted);
      }
    }
  }

  const auto labels = mappedInput.mapLabels("« Back", "Select", "", "");

  renderer.displayBuffer();
}

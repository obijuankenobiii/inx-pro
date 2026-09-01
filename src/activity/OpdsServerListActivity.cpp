#include "OpdsServerListActivity.h"

#include <GfxRenderer.h>

#include "activity/page/SubPage.h"
#include "activity/util/KeyboardEntryActivity.h"
#include "browser/OpdsBookBrowserActivity.h"
#include "images/Trash.h"
#include "state/OpdsServerStore.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiLayout.h"

namespace {
constexpr int kListItemHeight = Page::LIST_ITEM_HEIGHT;
}

/** Static trampoline that dispatches to the instance's displayTaskLoop. */
void OpdsServerListActivity::taskTrampoline(void* param) {
  auto* self = static_cast<OpdsServerListActivity*>(param);
  self->displayTaskLoop();
}

/** Loads the OPDS server list and starts the display task. */
void OpdsServerListActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  selectedIndex = 0;
  updateRequired = true;
  addingServer = false;
  newServerName.clear();
  newServerUrl.clear();
  newServerUsername.clear();
  newServerPassword.clear();

  renderer.syncWriteBufferFromActive();

  xTaskCreate(&OpdsServerListActivity::taskTrampoline, "OpdsServerListTask", 4096, this, 1, &displayTaskHandle);
}

/** Stops the display task and cleans up rendering resources. */
void OpdsServerListActivity::onExit() {
  ActivityWithSubactivity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

/** Handles input for navigating and selecting an OPDS server. */
void OpdsServerListActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (SubPage::closeInput(renderer, mappedInput, onBack)) return;

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
      const int listTop = UiLayout::PAGE_HEADER_HEIGHT;
      const int itemCount = static_cast<int>(OPDS_STORE.getAllServers().size()) + 1;
      const int tappedIndex = (tapY - listTop) / kListItemHeight;
      if (tapY >= listTop && tapY < listTop + itemCount * kListItemHeight && tappedIndex >= 0 &&
          tappedIndex < itemCount) {
        selectedIndex = tappedIndex;
        if (tappedIndex > 0 && tapX >= renderer.getScreenWidth() - 60) {
          const auto& servers = OPDS_STORE.getAllServers();
          if (tappedIndex - 1 < static_cast<int>(servers.size())) {
            OPDS_STORE.removeServer(servers[static_cast<size_t>(tappedIndex - 1)].name);
            selectedIndex = std::min(selectedIndex, static_cast<int>(OPDS_STORE.getAllServers().size()));
            updateRequired = true;
          }
          return;
        }
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

  {
    const int count = OPDS_STORE.getAllServers().size() + 1;
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) || mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (count > 0) {
        selectedIndex = (selectedIndex - 1 + count) % count;
        updateRequired = true;
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) || mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (count > 0) {
        selectedIndex = (selectedIndex + 1) % count;
        updateRequired = true;
      }
    }
  }
}

/** Enters the book browser subactivity for the currently selected server. */
void OpdsServerListActivity::handleSelection() {
  const auto& servers = OPDS_STORE.getAllServers();
  if (selectedIndex == 0) {
    startAddServer();
    return;
  }
  const int serverIndex = selectedIndex - 1;
  if (serverIndex < 0 || serverIndex >= static_cast<int>(servers.size())) return;

  const std::string serverUrl = servers[serverIndex].url;
  const std::string serverUsername = servers[serverIndex].username;
  const std::string serverPassword = servers[serverIndex].password;

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  exitActivity();
  enterNewActivity(new OpdsBookBrowserActivity(
      renderer, mappedInput,
      [this] {
        exitActivity();
        renderer.syncWriteBufferFromActive();
        updateRequired = true;
      },
      serverUrl, serverUsername, serverPassword));
  xSemaphoreGive(renderingMutex);
}

void OpdsServerListActivity::startAddServer() {
  addingServer = true;
  newServerName.clear();
  newServerUrl = "https://";
  newServerUsername.clear();
  newServerPassword.clear();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  exitActivity();
  enterNewActivity(new KeyboardEntryActivity(
      renderer, mappedInput, "OPDS Server Name", "", 10, 63, false,
      [this](const std::string& value) {
        newServerName = value;
        exitActivity();
        enterNewActivity(new KeyboardEntryActivity(
            renderer, mappedInput, "OPDS Server URL", newServerUrl, 10, 127, false,
            [this](const std::string& url) {
              newServerUrl = (url == "https://" || url == "http://") ? "" : url;
              exitActivity();
              enterNewActivity(new KeyboardEntryActivity(
                  renderer, mappedInput, "OPDS Username", "", 10, 63, false,
                  [this](const std::string& username) {
                    newServerUsername = username;
                    exitActivity();
                    enterNewActivity(new KeyboardEntryActivity(
                        renderer, mappedInput, "OPDS Password", "", 10, 63, true,
                        [this](const std::string& password) {
                          newServerPassword = password;
                          saveAddedServer();
                        },
                        [this]() { cancelAddServer(); }));
                  },
                  [this]() { cancelAddServer(); }));
            },
            [this]() { cancelAddServer(); }));
      },
      [this]() { cancelAddServer(); }));
  xSemaphoreGive(renderingMutex);
}

void OpdsServerListActivity::cancelAddServer() {
  exitActivity();
  addingServer = false;
  newServerName.clear();
  newServerUrl.clear();
  newServerUsername.clear();
  newServerPassword.clear();
  selectedIndex = 0;
  updateRequired = true;
}

void OpdsServerListActivity::saveAddedServer() {
  if (!newServerName.empty() && !newServerUrl.empty()) {
    OPDS_STORE.addServer(newServerName, newServerUrl, newServerUsername, newServerPassword);
  }
  cancelAddServer();
}

/** Background task loop that renders the screen when an update is required. */
void OpdsServerListActivity::displayTaskLoop() {
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

/** Renders the list of OPDS servers. */
void OpdsServerListActivity::render() {
  renderer.syncWriteBufferFromActive();
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const int listTop = SubPage::header(renderer, "OPDS Server");

  const auto& servers = OPDS_STORE.getAllServers();

  const int itemCount = static_cast<int>(servers.size()) + 1;
  for (int i = 0; i < itemCount; i++) {
    const int itemY = listTop + i * kListItemHeight;
    const int textY = itemY + (kListItemHeight - renderer.text.getLineHeight(systemFontId())) / 2;
    const char* name = i == 0 ? "Add OPDS server" : servers[i - 1].name.c_str();
    renderer.text.render(systemFontId(), 20, textY, name, true);
    if (i > 0) {
      constexpr int trashSize = 40;
      renderer.bitmap.icon(Trash, pageWidth - 60, itemY + (kListItemHeight - trashSize) / 2, trashSize, trashSize);
    }
    if (i + 1 < itemCount) {
      renderer.line.render(0, itemY + kListItemHeight - 1, pageWidth, itemY + kListItemHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }

  const auto labels = mappedInput.mapLabels("« Back", itemCount > 0 ? "Select" : "", "", "");

  renderer.displayBuffer();
}

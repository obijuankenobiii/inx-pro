/**
 * @file OpdsBookBrowserActivity.cpp
 * @brief Definitions for OpdsBookBrowserActivity.
 */

#include "OpdsBookBrowserActivity.h"

#include <Epub.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <OpdsStream.h>
#include <WiFi.h>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Button.h"
#include "activity/network/WifiSelectionActivity.h"
#include "network/HttpDownloader.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"
#include "util/StringUtils.h"
#include "util/UrlUtils.h"

namespace {
constexpr int PAGE_ITEMS = 8;
constexpr int SKIP_PAGE_MS = 700;
constexpr int kListItemHeight = Page::LIST_ITEM_HEIGHT;
constexpr int kBodyTop = FREEINK_DEVICE_X4PRO ? 80 : 70;

ButtonBounds retryButtonBounds(const GfxRenderer& renderer) {
  const int width = Button::width(renderer, "Retry", systemFontId());
  return {renderer.getScreenWidth() - width - 20, renderer.getScreenHeight() - Button::height - 20, width,
          Button::height};
}

bool contains(const ButtonBounds& bounds, const int x, const int y) {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}
}  // namespace

/** Static trampoline that dispatches to the instance's displayTaskLoop. */
void OpdsBookBrowserActivity::taskTrampoline(void* param) {
  auto* self = static_cast<OpdsBookBrowserActivity*>(param);
  self->displayTaskLoop();
}

/** Starts the display task and checks WiFi connectivity before loading the feed. */
void OpdsBookBrowserActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  state = BrowserState::CHECK_WIFI;
  entries.clear();
  navigationHistory.clear();
  currentPath = "";
  selectorIndex = 0;
  errorMessage.clear();
  statusMessage = "Checking WiFi...";
  updateRequired = true;

  const bool wifiAlreadyConnected =
      WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
  if (!wifiAlreadyConnected) {
    // Create the child before the browser renderer task. Otherwise the browser
    // can paint one frame while the Wi-Fi selector is taking ownership of the
    // same framebuffer, producing a zoomed/doubled transition.
    checkAndConnectWifi();
  }

  xTaskCreate(&OpdsBookBrowserActivity::taskTrampoline, "OpdsBookBrowserTask", 4096, this, 1, &displayTaskHandle);

  if (wifiAlreadyConnected) {
    checkAndConnectWifi();
  }
}

/** Stops the display task and clears loaded catalog data. */
void OpdsBookBrowserActivity::onExit() {
  ActivityWithSubactivity::onExit();

  WiFi.mode(WIFI_OFF);

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  entries.clear();
  navigationHistory.clear();
}

/** Handles input for browsing the catalog and downloading books. */
void OpdsBookBrowserActivity::loop() {
  if (state == BrowserState::WIFI_SELECTION) {
    ActivityWithSubactivity::loop();
    return;
  }

  if (SubPage::closeInput(renderer, mappedInput, onGoToHome)) return;

  if (state == BrowserState::ERROR) {
    if (mappedInput.hasTouch()) {
      float tapNx = 0.0f;
      float tapNy = 0.0f;
      if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
        const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
        const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
        if (contains(retryButtonBounds(renderer), tapX, tapY)) {
          state = BrowserState::LOADING;
          statusMessage = "Loading...";
          updateRequired = true;
          fetchFeed(currentPath);
        }
        return;
      }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        INX_SERIAL.printf("[%lu] [OPDS] Retry: WiFi connected, retrying fetch\n", millis());
        state = BrowserState::LOADING;
        statusMessage = "Loading...";
        updateRequired = true;
        fetchFeed(currentPath);
      } else {
        INX_SERIAL.printf("[%lu] [OPDS] Retry: WiFi not connected, launching selection\n", millis());
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    }
    return;
  }

  if (state == BrowserState::CHECK_WIFI) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoToHome();
    }
    return;
  }

  if (state == BrowserState::LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    }
    return;
  }

  if (state == BrowserState::DOWNLOADING) {
    return;
  }

  if (state == BrowserState::BROWSING) {
    if (mappedInput.hasTouch()) {
      float tapNx = 0.0f;
      float tapNy = 0.0f;
      if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
        const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
        const int pageStartIndex = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;
        const int visibleIndex = (tapY - kBodyTop) / kListItemHeight;
        const int tappedIndex = pageStartIndex + visibleIndex;
        if (tapY >= kBodyTop && visibleIndex >= 0 && visibleIndex < PAGE_ITEMS &&
            tappedIndex >= 0 && tappedIndex < static_cast<int>(entries.size())) {
          selectorIndex = tappedIndex;
          const auto& entry = entries[static_cast<size_t>(tappedIndex)];
          if (entry.type == OpdsEntryType::BOOK) {
            downloadBook(entry);
          } else {
            navigateToEntry(entry);
          }
        }
        return;
      }
    }
    const bool prevReleased = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Left);
    const bool nextReleased = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Right);
    const bool skipPage = mappedInput.getHeldTime() > SKIP_PAGE_MS;

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!entries.empty()) {
        const auto& entry = entries[selectorIndex];
        if (entry.type == OpdsEntryType::BOOK) {
          downloadBook(entry);
        } else {
          navigateToEntry(entry);
        }
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    } else if (prevReleased && !entries.empty()) {
      if (skipPage) {
        selectorIndex = ((selectorIndex / PAGE_ITEMS - 1) * PAGE_ITEMS + entries.size()) % entries.size();
      } else {
        selectorIndex = (selectorIndex + entries.size() - 1) % entries.size();
      }
      updateRequired = true;
    } else if (nextReleased && !entries.empty()) {
      if (skipPage) {
        selectorIndex = ((selectorIndex / PAGE_ITEMS + 1) * PAGE_ITEMS) % entries.size();
      } else {
        selectorIndex = (selectorIndex + 1) % entries.size();
      }
      updateRequired = true;
    }
  }
}

/** Background task loop that renders the screen when an update is required. */
void OpdsBookBrowserActivity::displayTaskLoop() {
  while (true) {
    // The Wi-Fi selector owns the renderer while it is open. Rendering the
    // browser underneath it races the selector's display task and can leave
    // its "Searching for connections..." frame ghosted over the next page.
    if (subActivity) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/** Renders the current browser state (loading, error, catalog list, etc). */
void OpdsBookBrowserActivity::render() const {
  renderer.syncWriteBufferFromActive();
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const int bodyTop = SubPage::header(renderer, "OPDS Browser");

  if (state == BrowserState::CHECK_WIFI) {
    renderer.text.centered(systemFontId(), pageHeight / 2, statusMessage.c_str());
    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::LOADING) {
    renderer.text.centered(systemFontId(), pageHeight / 2, statusMessage.c_str());
    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::ERROR) {
    renderer.text.centered(systemFontId(), pageHeight / 2 - 20, "Error:");
    renderer.text.centered(systemFontId(), pageHeight / 2 + 10, errorMessage.c_str());
    Button::render(renderer, retryButtonBounds(renderer), "Retry", true, systemFontId());
    const auto labels = mappedInput.mapLabels("« Back", "Retry", "", "");
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::DOWNLOADING) {
    renderer.text.centered(systemFontId(), pageHeight / 2 - 40, "Downloading...");
    renderer.text.centered(systemFontId(), pageHeight / 2 - 10, statusMessage.c_str());
    if (downloadTotal > 0) {
      const int barWidth = pageWidth - 100;
      constexpr int barHeight = 20;
      constexpr int barX = 50;
      const int barY = pageHeight / 2 + 20;
      ScreenComponents::drawProgressBar(renderer, barX, barY, barWidth, barHeight, downloadProgress, downloadTotal);
    }
    renderer.displayBuffer();
    return;
  }

  const char* confirmLabel = "Open";
  if (!entries.empty() && entries[selectorIndex].type == OpdsEntryType::BOOK) {
    confirmLabel = "Download";
  }
  const auto labels = mappedInput.mapLabels("« Back", confirmLabel, "", "");

  if (entries.empty()) {
    renderer.text.centered(systemFontId(), bodyTop + (pageHeight - bodyTop - 80) / 2,
                           "No entries found");
    renderer.displayBuffer();
    return;
  }

  const auto pageStartIndex = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;

  for (size_t i = pageStartIndex; i < entries.size() && i < static_cast<size_t>(pageStartIndex + PAGE_ITEMS); i++) {
    const auto& entry = entries[i];
    const int visibleIndex = static_cast<int>(i - pageStartIndex);
    const int itemY = bodyTop + visibleIndex * kListItemHeight;
    const bool isSelected = i == static_cast<size_t>(selectorIndex);
    if (isSelected) {
      renderer.rectangle.fill(0, itemY, pageWidth, kListItemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    std::string displayText;
    if (entry.type == OpdsEntryType::NAVIGATION) {
      displayText = "> " + entry.title;
    } else {
      displayText = entry.title;
      if (!entry.author.empty()) {
        displayText += " - " + entry.author;
      }
    }

    auto item =
        renderer.text.truncate(systemFontId(), displayText.c_str(), renderer.getScreenWidth() - 40);
    const int textY = itemY + (kListItemHeight - renderer.text.getLineHeight(systemFontId())) / 2;
    renderer.text.render(systemFontId(), 20, textY, item.c_str(), !isSelected);
    renderer.line.render(0, itemY + kListItemHeight - 1, pageWidth, itemY + kListItemHeight - 1, true,
                         LineRender::Style::Dotted);
  }

  renderer.displayBuffer();
}

/** Fetches and parses the OPDS feed at the given path. */
void OpdsBookBrowserActivity::fetchFeed(const std::string& path) {
  if (serverUrl.empty()) {
    state = BrowserState::ERROR;
    errorMessage = "No server URL configured";
    updateRequired = true;
    return;
  }

  std::string fullUrl = UrlUtils::buildUrl(serverUrl, path);
  INX_SERIAL.printf("[%lu] [OPDS] Fetching: %s\n", millis(), fullUrl.c_str());

  OpdsParser parser;

  {
    OpdsParserStream stream{parser};
    if (!HttpDownloader::fetchUrl(fullUrl, stream, serverUsername, serverPassword)) {
      state = BrowserState::ERROR;
      errorMessage = "Failed to fetch feed";
      INX_SERIAL.printf("[%lu] [OPDS] Fetch failed for URL: %s\n", millis(), fullUrl.c_str());
      updateRequired = true;
      return;
    }
  }

  if (!parser) {
    state = BrowserState::ERROR;
    errorMessage = "Failed to parse feed";
    updateRequired = true;
    return;
  }

  entries = std::move(parser).getEntries();
  INX_SERIAL.printf("[%lu] [OPDS] Found %d entries\n", millis(), entries.size());
  selectorIndex = 0;

  if (entries.empty()) {
    state = BrowserState::ERROR;
    errorMessage = "No entries found";
    updateRequired = true;
    return;
  }

  state = BrowserState::BROWSING;
  updateRequired = true;
}

/** Navigates into a catalog or book entry. */
void OpdsBookBrowserActivity::navigateToEntry(const OpdsEntry& entry) {
  navigationHistory.push_back(currentPath);
  currentPath = entry.href;

  state = BrowserState::LOADING;
  statusMessage = "Loading...";
  entries.clear();
  selectorIndex = 0;
  updateRequired = true;

  fetchFeed(currentPath);
}

/** Navigates back to the previous catalog entry, or exits if at the root. */
void OpdsBookBrowserActivity::navigateBack() {
  if (navigationHistory.empty()) {
    onGoToHome();
  } else {
    currentPath = navigationHistory.back();
    navigationHistory.pop_back();

    state = BrowserState::LOADING;
    statusMessage = "Loading...";
    entries.clear();
    selectorIndex = 0;
    updateRequired = true;

    fetchFeed(currentPath);
  }
}

/** Downloads the given book entry to the SD card. */
void OpdsBookBrowserActivity::downloadBook(const OpdsEntry& book) {
  state = BrowserState::DOWNLOADING;
  statusMessage = book.title;
  downloadProgress = 0;
  downloadTotal = 0;
  updateRequired = true;

  std::string downloadUrl = UrlUtils::buildUrl(serverUrl, book.href);

  std::string baseName = book.title;
  if (!book.author.empty()) {
    baseName += " - " + book.author;
  }
  std::string filename = "/" + StringUtils::sanitizeFilename(baseName) + ".epub";

  INX_SERIAL.printf("[%lu] [OPDS] Downloading: %s -> %s\n", millis(), downloadUrl.c_str(), filename.c_str());

  const auto result = HttpDownloader::downloadToFile(downloadUrl, filename, serverUsername, serverPassword,
                                                     [this](const size_t downloaded, const size_t total) {
                                                       downloadProgress = downloaded;
                                                       downloadTotal = total;
                                                       updateRequired = true;
                                                     });

  if (result == HttpDownloader::OK) {
    INX_SERIAL.printf("[%lu] [OPDS] Download complete: %s\n", millis(), filename.c_str());

    Epub epub(filename, "/.system");
    epub.clearCache();
    INX_SERIAL.printf("[%lu] [OPDS] Cleared cache for: %s\n", millis(), filename.c_str());

    state = BrowserState::BROWSING;
    updateRequired = true;
  } else {
    state = BrowserState::ERROR;
    errorMessage = "Download failed";
    updateRequired = true;
  }
}

/** Checks WiFi status and either fetches the feed or launches WiFi selection. */
void OpdsBookBrowserActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = BrowserState::LOADING;
    statusMessage = "Loading...";
    updateRequired = true;
    fetchFeed(currentPath);
    return;
  }

  launchWifiSelection();
}

/** Enters the WiFi selection subactivity. */
void OpdsBookBrowserActivity::launchWifiSelection() {
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = BrowserState::WIFI_SELECTION;
  updateRequired = false;
  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
  xSemaphoreGive(renderingMutex);
}

/** Handles completion of the WiFi selection subactivity. */
void OpdsBookBrowserActivity::onWifiSelectionComplete(const bool connected) {
  exitActivity();

  if (connected) {
    INX_SERIAL.printf("[%lu] [OPDS] WiFi connected via selection, fetching feed\n", millis());
    state = BrowserState::LOADING;
    statusMessage = "Loading...";
    updateRequired = true;
    fetchFeed(currentPath);
  } else {
    INX_SERIAL.printf("[%lu] [OPDS] WiFi selection cancelled/failed\n", millis());

    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    state = BrowserState::ERROR;
    errorMessage = "WiFi connection failed";
    updateRequired = true;
  }
}

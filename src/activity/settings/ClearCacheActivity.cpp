/**
 * @file ClearCacheActivity.cpp
 * @brief Definitions for ClearCacheActivity.
 */

#include "ClearCacheActivity.h"
#include "system/UiLayout.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <strings.h>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Button.h"
#include "ReaderFontSettingsDraw.h"
#include "state/BookState.h"
#include "state/NetworkCredential.h"
#include "state/ReadingGoal.h"
#include "state/RecentBooks.h"
#include "state/Session.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/LibraryIndex.h"

namespace {
constexpr int kListItemHeight = Page::LIST_ITEM_HEIGHT;
constexpr int kActionButtonWidth = 180;
constexpr int kActionButtonHeight = Button::height;
constexpr int kActionButtonBottomMargin = 64;

ButtonBounds actionButtonBounds(const int screenWidth, const int screenHeight) {
  return {(screenWidth - kActionButtonWidth) / 2,
          screenHeight - kActionButtonBottomMargin - kActionButtonHeight,
          kActionButtonWidth,
          kActionButtonHeight};
}

bool isInside(const ButtonBounds& bounds, const int x, const int y) {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}

void drawActionButton(const GfxRenderer& renderer, const int screenWidth, const int screenHeight, const char* label,
                      const bool active) {
  const ButtonBounds bounds = actionButtonBounds(screenWidth, screenHeight);
  (void)active;
  Button::render(renderer, bounds, label, true, systemFontId());
}
}

void ClearCacheActivity::taskTrampoline(void* param) {
  auto* self = static_cast<ClearCacheActivity*>(param);
  self->displayTaskLoop();
}

void ClearCacheActivity::clearTaskTrampoline(void* param) {
  auto* self = static_cast<ClearCacheActivity*>(param);
  self->clearCache();
  self->clearTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void ClearCacheActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  state = WARNING;
  selectedGroup = -1;
  updateRequired = true;

  xTaskCreate(&ClearCacheActivity::taskTrampoline, "ClearCacheActivityTask", 4096, this, 1, &displayTaskHandle);
}

void ClearCacheActivity::onExit() {
  ActivityWithSubactivity::onExit();

  if (clearTaskHandle) {
    const unsigned long waitStart = millis();
    while (clearTaskHandle && millis() - waitStart < 2000) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (clearTaskHandle) {
      vTaskDelete(clearTaskHandle);
      clearTaskHandle = nullptr;
    }
  }

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void ClearCacheActivity::startClearTask() {
  if (clearTaskHandle) {
    return;
  }

  xTaskCreatePinnedToCore(&ClearCacheActivity::clearTaskTrampoline, "ClearCacheWorker", 8192, this, 1,
                          &clearTaskHandle, 1);

  if (!clearTaskHandle) {
    state = FAILED;
    updateRequired = true;
  }
}

void ClearCacheActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void ClearCacheActivity::render() {
  const auto pageHeight = renderer.getScreenHeight();
  const auto pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  const int bodyTop = SubPage::header(renderer, "Clear cache");

  if (state == WARNING) {
    constexpr const char* names[GROUP_COUNT] = {"Display", "Book", "Thumbnails", "Recent", "Library index",
                                                 "Network", "Daily Reading"};
    constexpr int rowH = kListItemHeight;
    const int listTop = bodyTop + 1;
    const int left = 20;
    for (int i = 0; i < GROUP_COUNT; ++i) {
      const int y = listTop + i * rowH;
      const bool focused = i == selectedGroup;
      if (focused) {
        renderer.rectangle.fill(0, y, pageWidth, rowH, static_cast<int>(GfxRenderer::FillTone::Ink));
      }
      const int font = systemFontId();
      const int textY = y + (rowH - renderer.text.getLineHeight(font)) / 2;
      renderer.text.render(font, left, textY, names[i], !focused, EpdFontFamily::REGULAR);
      ReaderFontSettingsDraw::drawToggleCheckbox(renderer, pageWidth - 24, y, rowH, focused, selectedGroups[i]);
      renderer.line.render(0, y + rowH - 1, pageWidth, y + rowH - 1, true, LineRender::Style::Dotted);
    }

    const bool actionFocused = selectedGroup == GROUP_COUNT;
    const ButtonBounds actionBounds = actionButtonBounds(pageWidth, pageHeight);
    drawActionButton(renderer, pageWidth, pageHeight, "Clear", actionFocused);

    if (!anyGroupSelected()) {
    renderer.text.centered(systemFontId(), actionBounds.y - 28, "Select a cache group");
    }

    const auto labels = mappedInput.mapLabels("\xC2\xAB Cancel", actionFocused ? "Clear" : "Toggle", "Up", "Down");
    renderer.displayBuffer();
    return;
  }

  if (state == CLEARING) {
    renderer.text.centered(systemFontId(), pageHeight / 2, "Clearing...", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    renderer.text.centered(systemFontId(), pageHeight / 2 - 20, "Cache cleared", true, EpdFontFamily::BOLD);
    String resultText = String(clearedCount) + " items removed";
    if (failedCount > 0) {
      resultText += ", " + String(failedCount) + " failed";
    }
    renderer.text.centered(systemFontId(), pageHeight / 2 + 10, resultText.c_str());

    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.text.centered(systemFontId(), pageHeight / 2 - 20, "Clear failed", true, EpdFontFamily::BOLD);
    renderer.text.centered(systemFontId(), pageHeight / 2 + 10, "Check serial output for details");

    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.displayBuffer();
    return;
  }
}

bool ClearCacheActivity::anyGroupSelected() const {
  for (int i = 0; i < GROUP_COUNT; ++i) {
    if (selectedGroups[i]) {
      return true;
    }
  }
  return false;
}

void ClearCacheActivity::clearCache() {
  INX_SERIAL.printf("[%lu] [CLEAR_CACHE] Clearing selected cache groups...\n", millis());

  clearedCount = 0;
  failedCount = 0;

  const auto tryRemoveTree = [&](const char* path) {
    if (!SdMan.exists(path)) {
      INX_SERIAL.printf("[%lu] [CLEAR_CACHE] %s not present\n", millis(), path);
      return;
    }
    INX_SERIAL.printf("[%lu] [CLEAR_CACHE] Removing %s\n", millis(), path);
    if (SdMan.removeDir(path)) {
      clearedCount++;
      INX_SERIAL.printf("[%lu] [CLEAR_CACHE] Removed %s\n", millis(), path);
    } else {
      failedCount++;
      INX_SERIAL.printf("[%lu] [CLEAR_CACHE] Failed to remove %s\n", millis(), path);
    }
  };

  const auto tryRemoveFile = [&](const char* path) {
    if (!SdMan.exists(path)) {
      INX_SERIAL.printf("[%lu] [CLEAR_CACHE] %s not present\n", millis(), path);
      return;
    }
    INX_SERIAL.printf("[%lu] [CLEAR_CACHE] Removing %s\n", millis(), path);
    if (SdMan.remove(path)) {
      clearedCount++;
      INX_SERIAL.printf("[%lu] [CLEAR_CACHE] Removed %s\n", millis(), path);
    } else {
      failedCount++;
      INX_SERIAL.printf("[%lu] [CLEAR_CACHE] Failed to remove %s\n", millis(), path);
    }
  };

  const auto isThumbnail = [](const char* name) {
    return strcasecmp(name, "thumb.jpg") == 0 || strcasecmp(name, "thumb.png") == 0 ||
           strcasecmp(name, "thumb.bmp") == 0;
  };

  std::function<void(const std::string&)> clearTreeExceptThumbnails;
  clearTreeExceptThumbnails = [&](const std::string& path) {
    FsFile dir = SdMan.open(path.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      return;
    }

    char name[128];
    dir.rewindDirectory();
    while (true) {
      FsFile entry = dir.openNextFile();
      if (!entry) break;

      entry.getName(name, sizeof(name));
      const std::string entryPath = path + "/" + name;
      if (entry.isDirectory()) {
        entry.close();
        clearTreeExceptThumbnails(entryPath);
        continue;
      }

      const bool keep = isThumbnail(name);
      entry.close();
      if (keep) continue;

      INX_SERIAL.printf("[%lu] [CLEAR_CACHE] Removing %s\n", millis(), entryPath.c_str());
      if (SdMan.remove(entryPath.c_str())) {
        clearedCount++;
      } else {
        failedCount++;
        INX_SERIAL.printf("[%lu] [CLEAR_CACHE] Failed to remove %s\n", millis(), entryPath.c_str());
      }
      yield();
    }
    dir.close();
  };

  const auto clearSystemTxtCaches = [&]() {
    FsFile root = SdMan.open("/.system");
    if (!root || !root.isDirectory()) {
      if (root) {
        root.close();
      }
      return;
    }
    char name[128];
    root.rewindDirectory();
    while (true) {
      FsFile entry = root.openNextFile();
      if (!entry) {
        break;
      }
      if (entry.isDirectory()) {
        entry.getName(name, sizeof(name));
        if (strncmp(name, "txt_", 4) == 0) {
          const std::string path = std::string("/.system/") + name;
          entry.close();
          clearTreeExceptThumbnails(path);
          yield();
          continue;
        }
      }
      entry.close();
      yield();
    }
    root.close();
  };

  if (selectedGroups[GROUP_DISPLAY]) {
    tryRemoveTree("/.system/cache");
    tryRemoveTree("/.display-cache");
  }
  if (selectedGroups[GROUP_BOOK]) {
    clearTreeExceptThumbnails("/.metadata/epub");
    clearTreeExceptThumbnails("/.metadata/xtc");
    clearSystemTxtCaches();
  }
  if (selectedGroups[GROUP_THUMBNAILS]) {
    std::function<void(const std::string&)> removeThumbnails;
    removeThumbnails = [&](const std::string& path) {
      FsFile dir = SdMan.open(path.c_str());
      if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return;
      }

      char name[128];
      dir.rewindDirectory();
      while (true) {
        FsFile entry = dir.openNextFile();
        if (!entry) break;
        entry.getName(name, sizeof(name));
        const std::string entryPath = path + "/" + name;
        if (entry.isDirectory()) {
          entry.close();
          removeThumbnails(entryPath);
          continue;
        }
        const bool remove = isThumbnail(name);
        entry.close();
        if (!remove) continue;
        INX_SERIAL.printf("[%lu] [CLEAR_CACHE] Removing thumbnail %s\n", millis(), entryPath.c_str());
        if (SdMan.remove(entryPath.c_str())) {
          clearedCount++;
        } else {
          failedCount++;
          INX_SERIAL.printf("[%lu] [CLEAR_CACHE] Failed to remove thumbnail %s\n", millis(), entryPath.c_str());
        }
        yield();
      }
      dir.close();
    };

    removeThumbnails("/.metadata/epub");
    removeThumbnails("/.metadata/xtc");
    removeThumbnails("/.system");
  }
  if (selectedGroups[GROUP_RECENT]) {
    tryRemoveFile("/.metadata/recent.bin");
    tryRemoveFile("/.metadata/books.bin");
  }
  if (selectedGroups[GROUP_LIBRARY_INDEX]) {
    const bool hadIndex = LibraryIndex::hasIndex();
    if (hadIndex) {
      INX_SERIAL.printf("[%lu] [CLEAR_CACHE] Removing library index\n", millis());
      if (LibraryIndex::deleteIndex()) {
        clearedCount++;
      } else {
        failedCount++;
        INX_SERIAL.printf("[%lu] [CLEAR_CACHE] Failed to remove library index\n", millis());
      }
    } else {
      INX_SERIAL.printf("[%lu] [CLEAR_CACHE] Library index not present\n", millis());
    }
  }
  if (selectedGroups[GROUP_NETWORK]) {
    tryRemoveFile("/.system/wifi.bin");
  }
  if (selectedGroups[GROUP_DAILY_READING]) {
    INX_SERIAL.printf("[%lu] [CLEAR_CACHE] Clearing daily reading data\n", millis());
    ReadingGoal::clear();
    clearedCount++;
  }

  INX_SERIAL.printf("[%lu] [CLEAR_CACHE] Done: %d removed, %d failed\n", millis(), clearedCount, failedCount);

  if (failedCount > 0 && clearedCount == 0) {
    state = FAILED;
    updateRequired = true;
    return;
  }

  if (selectedGroups[GROUP_RECENT]) {
    RECENT_BOOKS.clear(false);
    BOOK_STATE.clear(false);
  }
  if (selectedGroups[GROUP_NETWORK]) {
    (void)WIFI_STORE.loadFromFile();
  }

  state = SUCCESS;
  updateRequired = true;
}

void ClearCacheActivity::loop() {
  if (SubPage::closeInput(renderer, mappedInput, goBack)) return;

  if (state == WARNING) {
    if (mappedInput.hasTouch()) {
      float tapNx = 0.0f, tapNy = 0.0f;
      if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
        const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
        const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
        const int listTop = UiLayout::PAGE_HEADER_HEIGHT + 1;
        const int tappedRow = (tapY - listTop) / kListItemHeight;
        const ButtonBounds actionBounds = actionButtonBounds(renderer.getScreenWidth(), renderer.getScreenHeight());

        if (tapX >= 0 && tapX < renderer.getScreenWidth() && tapY >= listTop && tappedRow >= 0 &&
            tappedRow < GROUP_COUNT) {
          selectedGroup = -1;
          selectedGroups[tappedRow] = !selectedGroups[tappedRow];
          updateRequired = true;
          return;
        }

        if (isInside(actionBounds, tapX, tapY)) {
          if (!anyGroupSelected()) {
            updateRequired = true;
            return;
          }
          xSemaphoreTake(renderingMutex, portMAX_DELAY);
          state = CLEARING;
          xSemaphoreGive(renderingMutex);
          updateRequired = true;
          vTaskDelay(10 / portTICK_PERIOD_MS);
          startClearTask();
          return;
        }
      }
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      selectedGroup = (selectedGroup + GROUP_COUNT) % (GROUP_COUNT + 1);
      updateRequired = true;
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      selectedGroup = (selectedGroup + 1) % (GROUP_COUNT + 1);
      updateRequired = true;
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (selectedGroup >= 0 && selectedGroup < GROUP_COUNT) {
        selectedGroups[selectedGroup] = !selectedGroups[selectedGroup];
        updateRequired = true;
        return;
      }
      if (selectedGroup != GROUP_COUNT || !anyGroupSelected()) {
        updateRequired = true;
        return;
      }
      INX_SERIAL.printf("[%lu] [CLEAR_CACHE] User confirmed selected cache clear\n", millis());
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = CLEARING;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      vTaskDelay(10 / portTICK_PERIOD_MS);

      startClearTask();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      INX_SERIAL.printf("[%lu] [CLEAR_CACHE] User cancelled\n", millis());
      goBack();
    }
    return;
  }

  if (state == SUCCESS || state == FAILED) {
    if (mappedInput.hasTouch()) {
      float tapNx = 0.0f, tapNy = 0.0f;
      if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
        goBack();
        return;
      }
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }
}

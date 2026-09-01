/**
 * @file OtaUpdateActivity.cpp
 * @brief Definitions for OtaUpdateActivity.
 */

#include "OtaUpdateActivity.h"
#include "system/UiLayout.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <string>

#include "activity/page/components/global/Button.h"
#include "activity/page/SubPage.h"
#include "activity/network/WifiSelectionActivity.h"
#include "network/OtaUpdater.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

#include "esp_task_wdt.h"

namespace {
constexpr int kSourceItemHeight = Page::LIST_ITEM_HEIGHT;
constexpr int kFirmwareItemHeight = Page::LIST_ITEM_HEIGHT;
const std::string kEmptyPath;

bool hasBinExtension(const std::string& path) {
  if (path.length() < 4) {
    return false;
  }
  const size_t n = path.length();
  return (path[n - 4] == '.') && (path[n - 3] == 'b' || path[n - 3] == 'B') &&
         (path[n - 2] == 'i' || path[n - 2] == 'I') && (path[n - 1] == 'n' || path[n - 1] == 'N');
}

std::string joinSdPath(const char* dirPath, const char* name) {
  std::string n = name ? name : "";
  if (n.empty()) {
    return "";
  }
  if (n[0] == '/') {
    return n;
  }
  std::string d = dirPath ? dirPath : "/";
  if (d.empty() || d == "/") {
    return "/" + n;
  }
  if (d.back() == '/') {
    return d + n;
  }
  return d + "/" + n;
}

std::string fileNameFromPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    return path;
  }
  return path.substr(slash + 1);
}

std::string formatBytes(const size_t bytes) {
  char buffer[24];
  if (bytes >= 1024 * 1024) {
    snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else if (bytes >= 1024) {
    snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
  } else {
    snprintf(buffer, sizeof(buffer), "%u B", static_cast<unsigned>(bytes));
  }
  return std::string(buffer);
}

void drawUpdateProgressCard(const GfxRenderer& renderer, const int pageWidth, const int bodyTop, const int screenHeight,
                            const float progress, const size_t processedBytes, const size_t totalBytes) {
  const int centerY = bodyTop + (screenHeight - bodyTop - 80) / 2;

  renderer.text.centered(MONTSERRAT_8_FONT_ID, centerY - 92, "INSTALLING UPDATE", true, EpdFontFamily::BOLD);
  renderer.text.centered(MONTSERRAT_14_FONT_ID, centerY - 54, "Installing firmware", true,
                         EpdFontFamily::BOLD);
  renderer.text.centered(systemFontId(), centerY - 10, "Please keep the device powered on.", true,
                         EpdFontFamily::REGULAR);

  const int barW = std::min(300, pageWidth - 72);
  constexpr int barH = 6;
  const int barX = (pageWidth - barW) / 2;
  const int barY = centerY + 28;
  renderer.rectangle.render(barX, barY, barW, barH, true);

  const int clamped = std::max(0, std::min(100, static_cast<int>(progress * 100.0f + 0.5f)));
  const int innerW = std::max(1, barW - 2);
  const int fillW = innerW * clamped / 100;
  renderer.rectangle.fill(barX + 1, barY + 1, innerW, barH - 2, false);
  if (fillW > 0) {
    renderer.rectangle.fill(barX + 1, barY + 1, fillW, barH - 2, true);
  }

  std::string metaLine;
  if (totalBytes > 0) {
    metaLine = formatBytes(processedBytes) + " / " + formatBytes(totalBytes);
  } else {
    metaLine = "Preparing package";
  }
  renderer.text.centered(MONTSERRAT_8_FONT_ID, barY + 26, metaLine.c_str(), true, EpdFontFamily::REGULAR);
}

void drawUpdateListRow(const GfxRenderer& renderer, const int pageWidth, const int itemY, const char* title,
                       const char* value, const bool selected, const bool drawDivider = true) {
  if (selected) {
    renderer.rectangle.fill(0, itemY, pageWidth, kFirmwareItemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
  }

  constexpr int labelFont = MONTSERRAT_8_FONT_ID;
  const int valueFont = systemFontId();
  const int labelY = itemY + 7;
  const int valueY = itemY + 25;
  const int valueMaxW = pageWidth - 40;
  const std::string clippedValue = renderer.text.truncate(valueFont, value ? value : "", valueMaxW);

  renderer.text.render(labelFont, 20, labelY, title, !selected, EpdFontFamily::BOLD);
  renderer.text.render(valueFont, 20, valueY, clippedValue.c_str(), !selected, EpdFontFamily::REGULAR);
  if (drawDivider) {
    renderer.line.render(0, itemY + kFirmwareItemHeight - 1, pageWidth, itemY + kFirmwareItemHeight - 1, true,
                         LineRender::Style::Dotted);
  }
}

ButtonBounds updateButtonBounds(const GfxRenderer& renderer, const int y) {
  const int font = systemFontId();
  const int width = Button::width(renderer, "Update", font);
  return {(renderer.getScreenWidth() - width) / 2, y, width, Button::height};
}
}

void OtaUpdateActivity::taskTrampoline(void* param) {
  auto* self = static_cast<OtaUpdateActivity*>(param);
  self->displayTaskLoop();
}

void OtaUpdateActivity::onWifiSelectionComplete(const bool success) {
  exitActivity();

  if (!success) {
    INX_SERIAL.printf("[%lu] [OTA] WiFi connection failed, exiting\n", millis());
    goBack();
    return;
  }

  INX_SERIAL.printf("[%lu] [OTA] WiFi connected, checking for update\n", millis());

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = CHECKING_FOR_UPDATE;
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
  vTaskDelay(pdMS_TO_TICKS(450));

  const auto res = updater.checkForUpdate();
  if (res == OtaUpdater::NO_UPDATE) {
    INX_SERIAL.printf("[%lu] [OTA] No stable update available\n", millis());
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = NO_UPDATE;
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }
  if (res != OtaUpdater::OK) {
    INX_SERIAL.printf("[%lu] [OTA] Update check failed: %d\n", millis(), res);
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = FAILED;
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  if (!updater.isUpdateNewer()) {
    INX_SERIAL.printf("[%lu] [OTA] No new update available\n", millis());
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = NO_UPDATE;
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = WAITING_CONFIRMATION;
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
}

void OtaUpdateActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  state = SOURCE_SELECTION;
  sourceSelectedIndex = 0;
  sourceSelectionVisible = false;
  sdFirmwareSelectionVisible = false;
  updateRequired = true;

  xTaskCreate(&OtaUpdateActivity::taskTrampoline, "OtaUpdateActivityTask", 4096, this, 1, &displayTaskHandle);

  INX_SERIAL.printf("[%lu] [OTA] Waiting for update source selection\n", millis());
}

void OtaUpdateActivity::scanSdFirmwareFiles() {
  sdFirmwareFiles.clear();
  sdFirmwareSelectedIndex = 0;
  sdFirmwareSelectionVisible = false;
  sdFirmwareScrollOffset = 0;

  auto scanDir = [this](const char* dirPath) {
    FsFile dir = SdMan.open(dirPath);
    if (!dir || !dir.isDirectory()) {
      if (dir) {
        dir.close();
      }
      return;
    }

    for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if (!file.isDirectory()) {
        char name[160] = {};
        file.getName(name, sizeof(name));
        const std::string path = joinSdPath(dirPath, name);
        if (hasBinExtension(path)) {
          sdFirmwareFiles.push_back(path);
        }
      }
      file.close();
    }
    dir.close();
  };

  scanDir("/");
  scanDir("/firmware");

  std::sort(sdFirmwareFiles.begin(), sdFirmwareFiles.end());
  sdFirmwareFiles.erase(std::unique(sdFirmwareFiles.begin(), sdFirmwareFiles.end()), sdFirmwareFiles.end());
}

const std::string& OtaUpdateActivity::selectedSdFirmwarePath() const {
  if (sdFirmwareSelectedIndex < 0 || sdFirmwareSelectedIndex >= static_cast<int>(sdFirmwareFiles.size())) {
    return kEmptyPath;
  }
  return sdFirmwareFiles[static_cast<size_t>(sdFirmwareSelectedIndex)];
}

void OtaUpdateActivity::onExit() {
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

void OtaUpdateActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired || updater.getRender()) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void OtaUpdateActivity::render() {
  if (subActivity) {
    return;
  }

  float updaterProgress = 0;
  if (state == UPDATE_IN_PROGRESS && updater.getTotalSize() > 0) {
    updaterProgress = static_cast<float>(updater.getProcessedSize()) / static_cast<float>(updater.getTotalSize());
  }

  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int startY = 0;
  const int dividerY = SubPage::header(renderer, "Update");

  const int bodyTop = dividerY;

  if (state == SOURCE_SELECTION) {
    constexpr const char* items[] = {"Online update", "SD card firmware"};
    for (int i = 0; i < 2; ++i) {
      const int itemY = bodyTop + i * kSourceItemHeight;
      const bool selected = sourceSelectionVisible && sourceSelectedIndex == i;
      if (selected) {
        renderer.rectangle.fill(0, itemY, pageWidth, kSourceItemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
      }
      const int textY = itemY + (kSourceItemHeight - renderer.text.getLineHeight(systemFontId())) / 2;
      renderer.text.render(systemFontId(), 20, textY, items[i], !selected, EpdFontFamily::REGULAR);
      if (i + 1 < 2) {
        renderer.line.render(0, itemY + kSourceItemHeight - 1, pageWidth, itemY + kSourceItemHeight - 1, true,
                             LineRender::Style::Dotted);
      }
    }
    const auto labels = mappedInput.mapLabels("« Back", "Select", "", "");
  } else if (state == WIFI_SELECTION) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.text.centered(systemFontId(), centerY, "Choose a network above.", true,
                           EpdFontFamily::REGULAR);
    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
  } else if (state == CHECKING_FOR_UPDATE) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.text.centered(systemFontId(), centerY, "This may take a moment.", true,
                           EpdFontFamily::REGULAR);
  } else if (state == WAITING_CONFIRMATION) {
    const std::string sizeLine =
        updater.getOtaSize() > 0 ? formatBytes(updater.getOtaSize()) : std::string("Firmware package");
    drawUpdateListRow(renderer, pageWidth, bodyTop, "CURRENT VERSION", INX_VERSION, false);
    drawUpdateListRow(renderer, pageWidth, bodyTop + kFirmwareItemHeight, "AVAILABLE UPDATE",
                      updater.getLatestVersion().c_str(), true);
    drawUpdateListRow(renderer, pageWidth, bodyTop + kFirmwareItemHeight * 2, "PACKAGE", sizeLine.c_str(), false,
                      false);
    Button::render(renderer, updateButtonBounds(renderer, bodyTop + kFirmwareItemHeight * 3 + 20), "Update", true,
                   systemFontId());
    const auto labels = mappedInput.mapLabels("Cancel", "Update", "", "");
  } else if (state == WAITING_SD_SELECTION) {
    const int totalFiles = static_cast<int>(sdFirmwareFiles.size());
    if (totalFiles == 0) {
      renderer.text.render(systemFontId(), 20, bodyTop, "No firmware .bin files found.", true,
                           EpdFontFamily::BOLD);
      renderer.text.render(systemFontId(), 20, bodyTop + 32, "Put .bin files in / or /firmware.",
                           true, EpdFontFamily::REGULAR);
      const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    } else {
      const int listBottom = screenHeight - 44;
      const int visibleRows = std::max(1, (listBottom - bodyTop) / kFirmwareItemHeight);
      if (sdFirmwareSelectedIndex < sdFirmwareScrollOffset) {
        sdFirmwareScrollOffset = sdFirmwareSelectedIndex;
      } else if (sdFirmwareSelectedIndex >= sdFirmwareScrollOffset + visibleRows) {
        sdFirmwareScrollOffset = sdFirmwareSelectedIndex - visibleRows + 1;
      }
      const int maxScroll = std::max(0, totalFiles - visibleRows);
      sdFirmwareScrollOffset = std::max(0, std::min(sdFirmwareScrollOffset, maxScroll));

      const int endIndex = std::min(totalFiles, sdFirmwareScrollOffset + visibleRows);
      for (int i = sdFirmwareScrollOffset; i < endIndex; ++i) {
        const int itemY = bodyTop + (i - sdFirmwareScrollOffset) * kFirmwareItemHeight;
        const bool selected = sdFirmwareSelectionVisible && sdFirmwareSelectedIndex == i;
        if (selected) {
          renderer.rectangle.fill(0, itemY, pageWidth, kFirmwareItemHeight,
                                  static_cast<int>(GfxRenderer::FillTone::Ink));
        }
        const std::string label = renderer.text.truncate(
            systemFontId(), sdFirmwareFiles[static_cast<size_t>(i)].c_str(), pageWidth - 40);
        const int textY =
            itemY + (kFirmwareItemHeight - renderer.text.getLineHeight(systemFontId())) / 2;
        renderer.text.render(systemFontId(), 20, textY, label.c_str(), !selected,
                             EpdFontFamily::REGULAR);
        if (i + 1 < endIndex) {
          renderer.line.render(0, itemY + kFirmwareItemHeight - 1, pageWidth, itemY + kFirmwareItemHeight - 1, true,
                               LineRender::Style::Dotted);
        }
      }
      const auto labels = mappedInput.mapLabels("« Back", "Select", "Up", "Down");
    }
  } else if (state == WAITING_SD_CONFIRMATION) {
    const std::string& firmwarePath = selectedSdFirmwarePath();
    if (!firmwarePath.empty() && SdMan.exists(firmwarePath.c_str())) {
      FsFile file;
      size_t firmwareSize = 0;
      if (SdMan.openFileForRead("OTA", firmwarePath, file)) {
        firmwareSize = file.size();
        file.close();
      }
      const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
      const std::string fileName = renderer.text.truncate(systemFontId(),
                                                          fileNameFromPath(firmwarePath).c_str(), pageWidth - 56);
      renderer.text.centered(MONTSERRAT_8_FONT_ID, centerY - 92, "SD FIRMWARE", true, EpdFontFamily::BOLD);
      renderer.text.centered(MONTSERRAT_14_FONT_ID, centerY - 54, "Install update?", true,
                             EpdFontFamily::BOLD);
      renderer.text.centered(systemFontId(), centerY - 10, fileName.c_str(), true,
                             EpdFontFamily::REGULAR);
      renderer.text.centered(MONTSERRAT_8_FONT_ID, centerY + 18, formatBytes(firmwareSize).c_str(), true,
                             EpdFontFamily::REGULAR);
      renderer.text.centered(MONTSERRAT_8_FONT_ID, centerY + 58,
                             "Keep the device powered on during install.", true, EpdFontFamily::REGULAR);
      Button::render(renderer, updateButtonBounds(renderer, centerY + 88), "Update", true, systemFontId());
      const auto labels = mappedInput.mapLabels("Cancel", "Install", "", "");
    } else {
      const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
      renderer.text.centered(systemFontId(), centerY, "Firmware file is missing.", true,
                             EpdFontFamily::REGULAR);
      const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    }
  } else if (state == UPDATE_IN_PROGRESS) {
    drawUpdateProgressCard(renderer, pageWidth, bodyTop, screenHeight, updaterProgress, updater.getProcessedSize(),
                           updater.getTotalSize());
  } else if (state == NO_UPDATE) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.text.centered(systemFontId(), centerY, "No update available", true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
  } else if (state == FAILED) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.text.centered(systemFontId(), centerY, "Update failed", true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
  } else if (state == FINISHED) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.text.centered(systemFontId(), centerY, "Update complete", true, EpdFontFamily::BOLD);
    renderer.text.centered(systemFontId(), centerY + 50,
                           "Press and hold power button to turn back on", true, EpdFontFamily::REGULAR);
  } else if (state == SHUTTING_DOWN) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.text.centered(systemFontId(), centerY, "Update complete", true, EpdFontFamily::BOLD);
    renderer.text.centered(systemFontId(), centerY + 50,
                           "Press and hold power button to turn back on", true, EpdFontFamily::REGULAR);
  }

  renderer.displayBuffer();

  if (state == FINISHED) {
    state = SHUTTING_DOWN;
  }
}

void OtaUpdateActivity::installOnlineUpdate() {
  INX_SERIAL.printf("[%lu] [OTA] New update available, starting download...\n", millis());
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = UPDATE_IN_PROGRESS;
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
  vTaskDelay(10 / portTICK_PERIOD_MS);
  const auto res = updater.installUpdate();

  if (res != OtaUpdater::OK) {
    INX_SERIAL.printf("[%lu] [OTA] Update failed: %d\n", millis(), res);
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = FAILED;
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = FINISHED;
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
}

void OtaUpdateActivity::installSdUpdate() {
  const std::string& firmwarePath = selectedSdFirmwarePath();
  if (firmwarePath.empty() || !SdMan.exists(firmwarePath.c_str())) {
    return;
  }

  INX_SERIAL.printf("[%lu] [OTA] Installing firmware from SD: %s\n", millis(), firmwarePath.c_str());
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = UPDATE_IN_PROGRESS;
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
  vTaskDelay(10 / portTICK_PERIOD_MS);
  const auto res = updater.installUpdateFromSd(firmwarePath.c_str());

  if (res != OtaUpdater::OK) {
    INX_SERIAL.printf("[%lu] [OTA] SD update failed: %d\n", millis(), res);
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = FAILED;
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = FINISHED;
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
}

void OtaUpdateActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (SubPage::closeInput(renderer, mappedInput, goBack)) return;

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int screenWidth = renderer.getScreenWidth();
      const int screenHeight = renderer.getScreenHeight();
      const int tapX = static_cast<int>(tapNx * screenWidth);
      const int tapY = static_cast<int>(tapNy * screenHeight);
      const int bodyTop = UiLayout::PAGE_HEADER_HEIGHT;

      if (state == SOURCE_SELECTION && tapY >= bodyTop && tapY < bodyTop + 2 * kSourceItemHeight) {
        sourceSelectedIndex = (tapY - bodyTop) / kSourceItemHeight;
        if (sourceSelectedIndex == 0) {
          xSemaphoreTake(renderingMutex, portMAX_DELAY);
          state = WIFI_SELECTION;
          updateRequired = false;
          enterNewActivity(new WifiSelectionActivity(
              renderer, mappedInput, [this](const bool connected) { onWifiSelectionComplete(connected); }));
          xSemaphoreGive(renderingMutex);
        } else {
          scanSdFirmwareFiles();
          state = WAITING_SD_SELECTION;
          updateRequired = true;
        }
        return;
      }

      if (state == WAITING_SD_SELECTION) {
        const int totalFiles = static_cast<int>(sdFirmwareFiles.size());
        if (totalFiles > 0) {
          const int listBottom = screenHeight - 44;
          const int visibleRows = std::max(1, (listBottom - bodyTop) / kFirmwareItemHeight);
          if (tapY >= bodyTop && tapY < bodyTop + visibleRows * kFirmwareItemHeight) {
            const int tappedIndex = sdFirmwareScrollOffset + (tapY - bodyTop) / kFirmwareItemHeight;
            if (tappedIndex >= 0 && tappedIndex < totalFiles) {
              sdFirmwareSelectedIndex = tappedIndex;
              state = WAITING_SD_CONFIRMATION;
              updateRequired = true;
              return;
            }
          }
        }
      }

      if (state == WAITING_CONFIRMATION) {
        const ButtonBounds button = updateButtonBounds(renderer, bodyTop + kFirmwareItemHeight * 3 + 20);
        if (tapX >= button.x && tapX < button.x + button.width && tapY >= button.y && tapY < button.y + button.height) {
          installOnlineUpdate();
          return;
        }
      }

      if (state == WAITING_SD_CONFIRMATION) {
        const int centerY = bodyTop + (screenHeight - bodyTop - 80) / 2;
        const ButtonBounds button = updateButtonBounds(renderer, centerY + 88);
        if (tapX >= button.x && tapX < button.x + button.width && tapY >= button.y && tapY < button.y + button.height) {
          installSdUpdate();
          return;
        }
      }

      if ((state == FAILED || state == NO_UPDATE) && tapY >= bodyTop) {
        goBack();
        return;
      }

      (void)tapX;
    }
  }

  if (state == SOURCE_SELECTION) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) || mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      sourceSelectedIndex = sourceSelectedIndex == 0 ? 1 : 0;
      sourceSelectionVisible = true;
      updateRequired = true;
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (sourceSelectedIndex == 0) {
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        state = WIFI_SELECTION;
        updateRequired = false;
        INX_SERIAL.printf("[%lu] [OTA] Launching WifiSelectionActivity...\n", millis());
        enterNewActivity(new WifiSelectionActivity(
            renderer, mappedInput, [this](const bool connected) { onWifiSelectionComplete(connected); }));
        xSemaphoreGive(renderingMutex);
      } else {
        scanSdFirmwareFiles();
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        state = WAITING_SD_SELECTION;
        xSemaphoreGive(renderingMutex);
        updateRequired = true;
      }
      return;
    }
    return;
  }

  if (state == WIFI_SELECTION) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }

  if (state == WAITING_CONFIRMATION) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      installOnlineUpdate();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }

    return;
  }

  if (state == WAITING_SD_SELECTION) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = SOURCE_SELECTION;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      return;
    }

    const int totalFiles = static_cast<int>(sdFirmwareFiles.size());
    if (totalFiles == 0) {
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      sdFirmwareSelectedIndex = (sdFirmwareSelectedIndex + 1) % totalFiles;
      sdFirmwareSelectionVisible = true;
      updateRequired = true;
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      sdFirmwareSelectedIndex = (sdFirmwareSelectedIndex + totalFiles - 1) % totalFiles;
      sdFirmwareSelectionVisible = true;
      updateRequired = true;
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = WAITING_SD_CONFIRMATION;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      return;
    }

    return;
  }

  if (state == WAITING_SD_CONFIRMATION) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = WAITING_SD_SELECTION;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      return;
    }

    const std::string& firmwarePath = selectedSdFirmwarePath();
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && !firmwarePath.empty() &&
        SdMan.exists(firmwarePath.c_str())) {
      installSdUpdate();
    }
    return;
  }

  if (state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }

  if (state == NO_UPDATE) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }

  if (state == SHUTTING_DOWN) {
    ESP.restart();
  }
}

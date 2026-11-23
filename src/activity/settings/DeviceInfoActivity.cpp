/**
 * @file DeviceInfoActivity.cpp
 * @brief Definitions for DeviceInfoActivity.
 */

#include "DeviceInfoActivity.h"

#include <esp_idf_version.h>
#include <esp_mac.h>
#include <esp_system.h>

#include <GfxRenderer.h>
#include <WiFi.h>
#include <BoardConfig.h>

#include <algorithm>
#include <cstdio>

#include "activity/page/SubPage.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {

constexpr int kRowHeight = 100;
constexpr int kSideMargin = 20;
constexpr int kSubtitleGap = 5;
constexpr int kBottomMargin = 20;

int listTop() { return FREEINK_DEVICE_X4PRO ? 80 : 70; }

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

const char* displayControllerName(const BoardConfig::DisplayController controller) {
  switch (controller) {
    case BoardConfig::DisplayController::SSD1677:
      return "SSD1677";
    case BoardConfig::DisplayController::UC8179:
      return "UC8179";
    case BoardConfig::DisplayController::UC8279:
      return "UC8279";
    case BoardConfig::DisplayController::UC8253:
      return "UC8253";
    case BoardConfig::DisplayController::ED2208:
      return "ED2208";
    case BoardConfig::DisplayController::LgfxEpd:
      return "LGFX EPD";
    case BoardConfig::DisplayController::IT8951:
      return "IT8951";
    default:
      return "Unknown";
  }
}

}

void DeviceInfoActivity::buildRows() {
  rows.clear();
  rows.push_back({"FIRMWARE", INX_VERSION});
  rows.push_back({"BOARD", BoardConfig::ACTIVE.name});

  char displayLine[64];
  const uint8_t displayVariant = BoardConfig::ACTIVE.displayControllerVariant;
  if (BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279 ||
      BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8179) {
    snprintf(displayLine, sizeof(displayLine), "%s %ux%u, LUT_VER 0x%02X", displayControllerName(BoardConfig::ACTIVE.displayController),
             BoardConfig::ACTIVE.displayWidth, BoardConfig::ACTIVE.displayHeight, displayVariant);
  } else {
    snprintf(displayLine, sizeof(displayLine), "%s %ux%u", displayControllerName(BoardConfig::ACTIVE.displayController),
             BoardConfig::ACTIVE.displayWidth, BoardConfig::ACTIVE.displayHeight);
  }
  rows.push_back({"DISPLAY", displayLine});

  INX_SERIAL.printf("[%lu] [DEVICE-INFO] display controller=%s variant=0x%02X resolution=%ux%u\n", millis(),
                    displayControllerName(BoardConfig::ACTIVE.displayController), displayVariant,
                    BoardConfig::ACTIVE.displayWidth, BoardConfig::ACTIVE.displayHeight);

  char chipLine[64];
#if FREEINK_DEVICE_X4PRO
  constexpr const char* chipModel = "Espressif ESP32-S3R8";
#else
  const char* chipModel = ESP.getChipModel();
#endif
  snprintf(chipLine, sizeof(chipLine), "%s rev %d, %d cores @ %d MHz", chipModel, ESP.getChipRevision(),
           ESP.getChipCores(), ESP.getCpuFreqMHz());
  rows.push_back({"CHIP", chipLine});

  rows.push_back({"FLASH", formatBytes(ESP.getFlashChipSize())});
  rows.push_back({"PSRAM", formatBytes(ESP.getPsramSize())});
  rows.push_back({"FREE HEAP", formatBytes(ESP.getFreeHeap())});
  rows.push_back({"ESP-IDF", esp_get_idf_version()});



  uint8_t mac[6] = {};
  char macLine[18] = "Unavailable";
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    snprintf(macLine, sizeof(macLine), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
  }
  rows.push_back({"MAC ADDRESS", macLine});
}

void DeviceInfoActivity::onEnter() {
  buildRows();
  scrollOffset = 0;
  updateRequired = true;
}

void DeviceInfoActivity::loop() {
  if (SubPage::closeInput(renderer, mappedInput, onClose)) return;
  const int visibleRows = std::max(1, (renderer.getScreenHeight() - listTop() - kBottomMargin) / kRowHeight);
  const int maxScroll = std::max(0, static_cast<int>(rows.size()) - visibleRows);
  if (mappedInput.hasTouch()) {
    if (mappedInput.wasTouchSwipeUpForRenderer(renderer)) {
      scrollOffset = std::min(maxScroll, scrollOffset + 1);
      updateRequired = true;
    } else if (mappedInput.wasTouchSwipeDownForRenderer(renderer)) {
      scrollOffset = std::max(0, scrollOffset - 1);
      updateRequired = true;
    }
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    scrollOffset = std::max(0, scrollOffset - 1);
    updateRequired = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    scrollOffset = std::min(maxScroll, scrollOffset + 1);
    updateRequired = true;
  }
  if (updateRequired) {
    updateRequired = false;
    render();
  }
}

void DeviceInfoActivity::render() {
  renderer.clearScreen();
  SubPage::header(renderer, "Device Information");
  const int screenWidth = renderer.getScreenWidth();
  const int startY = listTop();
  const int visibleRows = std::max(1, (renderer.getScreenHeight() - startY - kBottomMargin) / kRowHeight);

  const int labelFont = systemFontIdForPointSize(8);
  const int valueFont = systemFontId();
  const int labelHeight = renderer.text.getLineHeight(labelFont);
  const int valueHeight = renderer.text.getLineHeight(valueFont);
  const int contentHeight = labelHeight + kSubtitleGap + valueHeight;

  for (int visible = 0; visible < visibleRows; ++visible) {
    const size_t index = static_cast<size_t>(scrollOffset + visible);
    if (index >= rows.size()) break;
    const int itemY = startY + visible * kRowHeight;
    const int labelY = itemY + (kRowHeight - contentHeight) / 2;
    const int valueY = labelY + labelHeight + kSubtitleGap;
    const int valueMaxW = screenWidth - 2 * kSideMargin;
    const std::string clippedValue = renderer.text.truncate(valueFont, rows[index].value.c_str(), valueMaxW);

    renderer.text.render(labelFont, kSideMargin, labelY, rows[index].label.c_str(), true, EpdFontFamily::BOLD);
    renderer.text.render(valueFont, kSideMargin, valueY, clippedValue.c_str(), true, EpdFontFamily::REGULAR);
    if (visible + 1 < visibleRows && index + 1 < rows.size()) {
      renderer.line.render(kSideMargin, itemY + kRowHeight - 1, screenWidth - kSideMargin,
                           itemY + kRowHeight - 1, true, LineRender::Style::Dotted);
    }
  }

  mappedInput.mapLabels("\xC2\xAB Back", "", "Up", "Down");
  renderer.displayBuffer();
}

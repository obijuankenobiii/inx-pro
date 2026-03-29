/**
 * @file DeviceInfoActivity.cpp
 * @brief Definitions for DeviceInfoActivity.
 */

#include "DeviceInfoActivity.h"

#include <esp_idf_version.h>
#include <esp_system.h>

#include <GfxRenderer.h>
#include <WiFi.h>
#include <BoardConfig.h>

#include <cstdio>

#include "activity/page/SubPage.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {

// Same formatter OtaUpdateActivity.cpp uses for firmware/package sizes - not shared since this is its only
// other use.
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

}  // namespace

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
  rows.push_back({"MAC ADDRESS", std::string(WiFi.macAddress().c_str())});
}

void DeviceInfoActivity::onEnter() {
  buildRows();
  updateRequired = true;
}

void DeviceInfoActivity::loop() {
  if (SubPage::closeInput(renderer, mappedInput, onClose)) return;
  if (updateRequired) {
    updateRequired = false;
    render();
  }
}

void DeviceInfoActivity::render() {
  renderer.clearScreen();
  const int startY = SubPage::header(renderer, "Device Information") + 20;
  const int screenWidth = renderer.getScreenWidth();
  const int rowHeight = rows.size() > 8
                            ? std::max(60, (renderer.getScreenHeight() - startY - 10) / static_cast<int>(rows.size()))
                            : 80;

  constexpr int labelFont = MONTSERRAT_8_FONT_ID;
  const int valueFont = systemFontId();

  for (size_t i = 0; i < rows.size(); ++i) {
    const int itemY = startY + static_cast<int>(i) * rowHeight;
    const int labelY = itemY + 7;
    const int valueY = itemY + 30;
    const int valueMaxW = screenWidth - 40;
    const std::string clippedValue = renderer.text.truncate(valueFont, rows[i].value.c_str(), valueMaxW);

    renderer.text.render(labelFont, 20, labelY, rows[i].label.c_str(), true, EpdFontFamily::BOLD);
    renderer.text.render(valueFont, 20, valueY, clippedValue.c_str(), true, EpdFontFamily::REGULAR);
  }

  renderer.displayBuffer();
}

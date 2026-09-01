/**
 * @file ClockStylePickerActivity.cpp
 * @brief Picker for date/time sleep screen clock designs.
 */

#include "ClockStylePickerActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include <ClockRender.h>

namespace {
ClockRender::DateTimeView previewDateTime() {
  ClockRender::DateTimeView dt;
  dt.year = 2026;
  dt.month = 6;
  dt.day = 2;
  dt.hour = 10;
  dt.minute = 24;
  dt.weekday = 2;
  return dt;
}
}

void ClockStylePickerActivity::taskTrampoline(void* param) {
  auto* self = static_cast<ClockStylePickerActivity*>(param);
  self->displayTaskLoop();
}

void ClockStylePickerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();

  selectedIndex = SETTINGS.sleepClockStyle;
  if (selectedIndex >= ClockRender::styleCount()) {
    selectedIndex = 0;
  }

  updateRequired = true;
  xTaskCreate(&ClockStylePickerActivity::taskTrampoline, "ClockStylePickerTask", 4096, this, 1, &displayTaskHandle);
}

void ClockStylePickerActivity::onExit() {
  ActivityWithSubactivity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void ClockStylePickerActivity::displayTaskLoop() {
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

void ClockStylePickerActivity::render() {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  const auto dt = previewDateTime();
  ClockRender::render(renderer, selectedIndex, dt, true, 0, 0, pageWidth, pageHeight);

  const char* name = ClockRender::styleName(selectedIndex);
  renderer.rectangle.fill(0, 0, pageWidth, 24, false);
  renderer.text.render(MONTSERRAT_16_FONT_ID, 8, 6, name, true, EpdFontFamily::BOLD);
  char countText[8];
  std::snprintf(countText, sizeof(countText), "%d/%d", selectedIndex + 1, ClockRender::styleCount());
  renderer.text.render(MONTSERRAT_16_FONT_ID,
                       pageWidth - renderer.text.getWidth(MONTSERRAT_16_FONT_ID, countText) - 8, 6, countText,
                       true);

  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Select", "Prev", "Next");
  renderer.displayBuffer();
}

void ClockStylePickerActivity::applySelection() {
  SETTINGS.sleepClockStyle = static_cast<uint8_t>(selectedIndex);
  SETTINGS.sleepScreen = SystemSetting::SLEEP_SCREEN_MODE::DATETIME;
  SETTINGS.saveToFile();
  onBack();
}

void ClockStylePickerActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int screenWidth = renderer.getScreenWidth();
      const int tapX = static_cast<int>(tapNx * screenWidth);
      if (tapX < screenWidth / 3) {
        selectedIndex = (selectedIndex + ClockRender::styleCount() - 1) % ClockRender::styleCount();
        updateRequired = true;
      } else if (tapX >= (screenWidth * 2) / 3) {
        selectedIndex = (selectedIndex + 1) % ClockRender::styleCount();
        updateRequired = true;
      } else {
        applySelection();
      }
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    applySelection();
    return;
  }

  bool needRedraw = false;

  if (mappedInput.wasPressed(MappedInputManager::Button::Up) || mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    selectedIndex = (selectedIndex + ClockRender::styleCount() - 1) % ClockRender::styleCount();
    needRedraw = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) || mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    selectedIndex = (selectedIndex + 1) % ClockRender::styleCount();
    needRedraw = true;
  }

  if (needRedraw) {
    updateRequired = true;
  }
}

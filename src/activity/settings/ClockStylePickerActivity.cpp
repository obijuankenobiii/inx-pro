/**
 * @file ClockStylePickerActivity.cpp
 * @brief Picker for date/time sleep screen clock designs.
 */

#include "ClockStylePickerActivity.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "activity/page/SubPage.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include <ClockRender.h>

namespace {
constexpr int kPickerSideInset = 20;
constexpr int kPickerTopInset = 10;
constexpr int kPickerBottomInset = 20;
constexpr int kPickerRowGap = 10;

int pickerContentTop() {
  constexpr int headerTop = FREEINK_DEVICE_X4PRO ? 20 : 15;
  return headerTop + 40 + 20 + kPickerTopInset;
}

int pickerRowHeight(const int screenHeight) {
  const int count = ClockRender::styleCount();
  return std::max(1, (screenHeight - pickerContentTop() - kPickerBottomInset - kPickerRowGap * (count - 1)) / count);
}

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
  const int styleCount = ClockRender::styleCount();
  const int contentTop = pickerContentTop();
  const int rowHeight = pickerRowHeight(pageHeight);

  renderer.clearScreen();
  SubPage::header(renderer, "Clock face");
  const auto dt = previewDateTime();
  for (int index = 0; index < styleCount; ++index) {
    const int rowY = contentTop + index * (rowHeight + kPickerRowGap);
    const bool selected = index == selectedIndex;
    const char* name = ClockRender::styleName(static_cast<uint8_t>(index));
    const int labelFont = systemFontId();
    const int labelY = rowY + 2;
    const int labelLineHeight = renderer.text.getLineHeight(labelFont);
    const int previewY = labelY + renderer.text.getLineHeight(labelFont) + 2;
    const int previewHeight = std::max(1, rowHeight - (previewY - rowY) - 2);

    if (index + 1 < styleCount) {
      const int dividerY = rowY + rowHeight + kPickerRowGap / 2;
      renderer.line.render(kPickerSideInset, dividerY, pageWidth - kPickerSideInset - 1, dividerY, true,
                           LineRender::Style::Dotted);
    }
    renderer.text.render(labelFont, kPickerSideInset, labelY, name, true, EpdFontFamily::REGULAR);
    if (selected) {
      constexpr int tagHorizontalPadding = 8;
      const char* activeText = "Active";
      const int tagHeight = labelLineHeight + 4;
      const int tagWidth = renderer.text.getWidth(labelFont, activeText) + tagHorizontalPadding * 2;
      const int tagX = pageWidth - kPickerSideInset - tagWidth;
      const int tagY = rowY;
      renderer.rectangle.fill(tagX, tagY, tagWidth, tagHeight, false);
      renderer.rectangle.render(tagX, tagY, tagWidth, tagHeight, true);
      renderer.text.render(labelFont, tagX + tagHorizontalPadding, tagY + 2, activeText, true,
                           EpdFontFamily::REGULAR);
    }
    int clockY = previewY;
    int clockHeight = previewHeight;
    constexpr int compactClockPadding = 20;
    if (clockHeight > compactClockPadding * 2) {
      clockY += compactClockPadding;
      clockHeight -= compactClockPadding * 2;
    }
    ClockRender::render(renderer, static_cast<uint8_t>(index), dt, true, kPickerSideInset, clockY,
                         std::max(1, pageWidth - kPickerSideInset * 2), clockHeight, true);
  }

  renderer.displayBuffer();
}

void ClockStylePickerActivity::applySelection() {
  SETTINGS.sleepClockStyle = static_cast<uint8_t>(selectedIndex);
  SETTINGS.saveToFile();
  onBack();
}

void ClockStylePickerActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (SubPage::closeInput(renderer, mappedInput, onBack)) return;

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int screenWidth = renderer.getScreenWidth();
      const int screenHeight = renderer.getScreenHeight();
      const int tapX = static_cast<int>(tapNx * screenWidth);
      const int rowHeight = pickerRowHeight(screenHeight);
      const int tapY = static_cast<int>(tapNy * screenHeight);
      const int contentTop = pickerContentTop();
      if (tapX >= kPickerSideInset && tapX < screenWidth - kPickerSideInset && tapY >= contentTop) {
        const int row = (tapY - contentTop) / (rowHeight + kPickerRowGap);
        const int rowY = contentTop + row * (rowHeight + kPickerRowGap);
        if (row >= 0 && row < ClockRender::styleCount() && tapY < rowY + rowHeight) {
          selectedIndex = row;
          applySelection();
        }
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

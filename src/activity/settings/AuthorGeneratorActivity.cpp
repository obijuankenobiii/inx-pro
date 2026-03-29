#include "AuthorGeneratorActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>

#include <algorithm>
#include <cstring>
#include <cstdio>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Button.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/AuthorIndex.h"

namespace {
constexpr int kButtonWidth = 180;
constexpr int kButtonBottomMargin = 64;

ButtonBounds buttonBounds(const GfxRenderer& renderer) {
  return {(renderer.getScreenWidth() - kButtonWidth) / 2,
          renderer.getScreenHeight() - kButtonBottomMargin - Button::height,
          kButtonWidth,
          Button::height};
}

bool contains(const ButtonBounds& bounds, const int x, const int y) {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}

void progressBar(const GfxRenderer& renderer, const int y, const int current, const int total) {
  const int width = std::min(300, renderer.getScreenWidth() - 72);
  const int x = (renderer.getScreenWidth() - width) / 2;
  renderer.rectangle.render(x, y, width, 6, true);
  renderer.rectangle.fill(x + 1, y + 1, std::max(1, width - 2), 4, false);
  if (total > 0 && current > 0) {
    const int fill = std::min(width - 2, (width - 2) * current / total);
    if (fill > 0) renderer.rectangle.fill(x + 1, y + 1, fill, 4, true);
  }
}
}  // namespace

void AuthorGeneratorActivity::workerTaskTrampoline(void* param) {
  static_cast<AuthorGeneratorActivity*>(param)->workerTaskLoop();
}

void AuthorGeneratorActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  state = READY;
  cancelRequested = false;
  updateRequired = true;
  processedCount = 0;
  totalCount = 0;
  currentPath[0] = '\0';
}

void AuthorGeneratorActivity::onExit() {
  cancelRequested = true;
  const unsigned long start = millis();
  while (workerTaskHandle && millis() - start < 1500) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (workerTaskHandle) {
    vTaskDelete(workerTaskHandle);
    workerTaskHandle = nullptr;
  }
  ActivityWithSubactivity::onExit();
}

void AuthorGeneratorActivity::startGeneration() {
  if (state == RUNNING || workerTaskHandle) return;
  state = RUNNING;
  cancelRequested = false;
  processedCount = 0;
  totalCount = 0;
  currentPath[0] = '\0';
  updateRequired = true;
  if (xTaskCreatePinnedToCore(&AuthorGeneratorActivity::workerTaskTrampoline, "AuthorGenWorker", 12288, this, 1,
                              &workerTaskHandle, 0) != pdPASS) {
    workerTaskHandle = nullptr;
    state = FAILED;
  }
}

void AuthorGeneratorActivity::workerTaskLoop() {
  const bool generated = AuthorIndex::generate(
      [this](const int current, const int total, const char* name) {
        processedCount = current;
        totalCount = total;
        if (name) strlcpy(currentPath, name, sizeof(currentPath));
        updateRequired = true;
      },
      [this] { return cancelRequested; });

  state = cancelRequested ? CANCELLED : (generated ? SUCCESS : FAILED);
  currentPath[0] = '\0';
  updateRequired = true;
  workerTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void AuthorGeneratorActivity::render() {
  renderer.clearScreen();
  const int contentTop = SubPage::header(renderer, "Authors");
  const int centerY = contentTop + (renderer.getScreenHeight() - contentTop - 80) / 2;
  const int font = systemFontId();

  if (state == READY) {
    renderer.text.centered(font, centerY - 58, "GENERATE AUTHORS", true, EpdFontFamily::BOLD);
    renderer.text.centered(font, centerY - 18, "Build author metadata for the library", true,
                           EpdFontFamily::REGULAR);
    renderer.text.centered(font, centerY + 12, "Existing metadata is replaced.", true, EpdFontFamily::REGULAR);
    progressBar(renderer, centerY + 48, 0, 0);
    Button::render(renderer, buttonBounds(renderer), "Generate", true, font);
    mappedInput.mapLabels("\xC2\xAB Back", "Generate", "", "");
  } else if (state == RUNNING) {
    renderer.text.centered(font, centerY - 58, "GENERATING AUTHORS", true, EpdFontFamily::BOLD);
    renderer.text.centered(font, centerY - 18, "Scanning library", true, EpdFontFamily::BOLD);
    char line[64];
    snprintf(line, sizeof(line), "Processed %d of %d books", static_cast<int>(processedCount),
             static_cast<int>(totalCount));
    renderer.text.centered(font, centerY + 16, line, true, EpdFontFamily::REGULAR);
    progressBar(renderer, centerY + 48, processedCount, totalCount);
    if (currentPath[0] != '\0') {
      renderer.text.centered(font, centerY + 78, currentPath, true, EpdFontFamily::REGULAR);
    }
    Button::render(renderer, buttonBounds(renderer), "Stop", true, font);
    mappedInput.mapLabels("Stop", "", "", "");
  } else {
    const bool success = state == SUCCESS;
    renderer.text.centered(font, centerY - 40, success ? "Authors ready" : (state == CANCELLED ? "Stopped" : "Failed"),
                           true, EpdFontFamily::BOLD);
    char line[64];
    snprintf(line, sizeof(line), "Processed %d books", static_cast<int>(processedCount));
    renderer.text.centered(font, centerY + 2, line, true, EpdFontFamily::REGULAR);
    renderer.text.centered(font, centerY + 42, "Tap to go back", true, EpdFontFamily::REGULAR);
    mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
  }
  renderer.displayBuffer();
}

void AuthorGeneratorActivity::loop() {
  if (SubPage::closeInput(renderer, mappedInput, goBack)) return;

  if (mappedInput.hasTouch()) {
    float nx = 0.0f;
    float ny = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, nx, ny)) {
      const int x = static_cast<int>(nx * renderer.getScreenWidth());
      const int y = static_cast<int>(ny * renderer.getScreenHeight());
      if (state == READY && contains(buttonBounds(renderer), x, y)) {
        startGeneration();
      } else if (state == RUNNING && contains(buttonBounds(renderer), x, y)) {
        cancelRequested = true;
        updateRequired = true;
      } else if (state != READY && state != RUNNING) {
        goBack();
      }
      return;
    }
  }

  if (state == READY) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) startGeneration();
    if (updateRequired) {
      updateRequired = false;
      render();
    }
    return;
  }
  if (state == RUNNING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      cancelRequested = true;
      updateRequired = true;
    }
    if (updateRequired) {
      updateRequired = false;
      render();
    }
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    goBack();
    return;
  }
  if (updateRequired) {
    updateRequired = false;
    render();
  }
}

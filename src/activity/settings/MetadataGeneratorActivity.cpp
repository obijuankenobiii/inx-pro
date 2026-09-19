#include "MetadataGeneratorActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Button.h"
#include "activity/settings/ReaderFontSettingsDraw.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiLayout.h"
#include "util/MetadataIndex.h"
#include "util/ThumbnailGeneration.h"

namespace {
constexpr int kOptionCount = 12;
constexpr int kRowHeight = UiLayout::LIST_ITEM_HEIGHT;
constexpr int kButtonWidth = 180;
constexpr int kButtonBottomMargin = 64;
constexpr const char* kOptionLabels[kOptionCount] = {
    "Thumbnails", "Authors", "Series", "Tags", "Publisher", "Language", "Rating", "Publication date",
    "Identifiers", "Title sort", "Author sort", "Description, dates, UUID + custom fields"};

ButtonBounds actionBounds(const GfxRenderer& renderer) {
  return {(renderer.getScreenWidth() - kButtonWidth) / 2,
          renderer.getScreenHeight() - kButtonBottomMargin - Button::height, kButtonWidth, Button::height};
}

bool contains(const ButtonBounds& bounds, const int x, const int y) {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}
}

void MetadataGeneratorActivity::workerTaskTrampoline(void* param) {
  static_cast<MetadataGeneratorActivity*>(param)->workerTaskLoop();
}

void MetadataGeneratorActivity::onEnter() {
  Activity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();
  state = READY;
  cancelRequested = false;
  thumbnailPhase = false;
  updateRequired = true;
  processedCount = 0;
  totalCount = 0;
  currentPath[0] = '\0';
  completionSummary[0] = '\0';
}

void MetadataGeneratorActivity::onExit() {
  cancelRequested = true;
  const unsigned long start = millis();
  while (workerTaskHandle && millis() - start < 1500) vTaskDelay(pdMS_TO_TICKS(10));
  if (workerTaskHandle) {
    vTaskDelete(workerTaskHandle);
    workerTaskHandle = nullptr;
  }
  if (renderingMutex) {
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }
  Activity::onExit();
}

bool MetadataGeneratorActivity::selectedAnything() const {
  for (const bool selected : selectedOptions) {
    if (selected) return true;
  }
  return false;
}

void MetadataGeneratorActivity::toggleOption(const int index) {
  if (index < 0 || index >= kOptionCount) return;
  selectedOptions[index] = !selectedOptions[index];
  updateRequired = true;
}

void MetadataGeneratorActivity::startGeneration() {
  if (state == RUNNING || workerTaskHandle || !selectedAnything()) return;
  cancelRequested = false;
  processedCount = 0;
  totalCount = 0;
  currentPath[0] = '\0';
  completionSummary[0] = '\0';
  thumbnailPhase = false;
  state = RUNNING;
  updateRequired = true;
  if (xTaskCreatePinnedToCore(&MetadataGeneratorActivity::workerTaskTrampoline, "MetadataIndexWorker", 16384, this, 1,
                              &workerTaskHandle, 0) != pdPASS) {
    workerTaskHandle = nullptr;
    state = FAILED;
  }
}

void MetadataGeneratorActivity::workerTaskLoop() {
  MetadataIndex::Options options;
  options.authors = selectedOptions[1];
  options.series = selectedOptions[2];
  options.tags = selectedOptions[3];
  options.publishers = selectedOptions[4];
  options.languages = selectedOptions[5];
  options.ratings = selectedOptions[6];
  options.publicationDates = selectedOptions[7];
  options.identifiers = selectedOptions[8];
  options.titleSorts = selectedOptions[9];
  options.authorSorts = selectedOptions[10];
  options.completeRecords = selectedOptions[11];
  const bool needsIndexes = options.authors || options.series || options.tags || options.publishers || options.languages ||
                            options.ratings || options.publicationDates || options.identifiers || options.titleSorts ||
                            options.authorSorts || options.completeRecords;
  bool indexed = !needsIndexes || MetadataIndex::generate(
      options,
      [this](const int current, const int total, const char* path) {
        processedCount = current;
        totalCount = total;
        if (path) strlcpy(currentPath, path, sizeof(currentPath));
        updateRequired = true;
      },
      [this] { return cancelRequested; });

  ThumbnailGeneration::Result thumbnailResult;
  if (indexed && selectedOptions[0] && !cancelRequested) {
    thumbnailPhase = true;
    processedCount = 0;
    totalCount = 0;
    strlcpy(currentPath, "Scanning books for missing thumbnails", sizeof(currentPath));
    updateRequired = true;
    indexed = ThumbnailGeneration::generate(
        renderer, renderingMutex, thumbnailResult,
        [this](const int current, const char* path) {
          processedCount = current;
          totalCount = 0;
          if (path) strlcpy(currentPath, path, sizeof(currentPath));
          updateRequired = true;
        },
        [this] { return cancelRequested; });
    std::snprintf(completionSummary, sizeof(completionSummary), "Thumbnails: %d new, %d existing, %d failed",
                  thumbnailResult.generated, thumbnailResult.skipped, thumbnailResult.failed);
  }

  if (cancelRequested) {
    state = CANCELLED;
  } else if (!indexed) {
    state = FAILED;
  } else {
    state = SUCCESS;
  }
  workerTaskHandle = nullptr;
  currentPath[0] = '\0';
  updateRequired = true;
  vTaskDelete(nullptr);
}

void MetadataGeneratorActivity::render() {
  if (renderingMutex && xSemaphoreTake(renderingMutex, 0) != pdTRUE) {
    updateRequired = true;
    return;
  }
  renderer.clearScreen();
  const int contentTop = SubPage::header(renderer, "Generate Metadata");
  const int screenWidth = renderer.getScreenWidth();
  const int font = systemFontId();

  if (state == READY) {
    optionListTop = contentTop + 1;
    const ButtonBounds bounds = actionBounds(renderer);
    optionListRows = std::max(1, (bounds.y - optionListTop - 8) / kRowHeight);
    scrollOffset = std::max(0, std::min(scrollOffset, kOptionCount - optionListRows));
    for (int row = 0; row < optionListRows && scrollOffset + row < kOptionCount; ++row) {
      const int index = scrollOffset + row;
      const int y = optionListTop + row * kRowHeight;
      const int textY = y + (kRowHeight - renderer.text.getLineHeight(font)) / 2;
      renderer.text.render(font, 20, textY, kOptionLabels[index], true, EpdFontFamily::REGULAR);
      ReaderFontSettingsDraw::drawToggleCheckbox(renderer, screenWidth - 24, y, kRowHeight, false,
                                                 selectedOptions[index]);
      renderer.line.render(0, y + kRowHeight - 1, screenWidth, y + kRowHeight - 1, true,
                           LineRender::Style::Dotted);
    }
    if (kOptionCount > optionListRows) {
      const int listHeight = optionListRows * kRowHeight;
      const int thumbHeight = std::max(12, optionListRows * listHeight / kOptionCount);
      const int thumbY = optionListTop + scrollOffset * listHeight / kOptionCount;
      renderer.rectangle.fill(screenWidth - 4, thumbY, 2, thumbHeight, true);
    }
    Button::render(renderer, bounds, "Generate", true, font);
    mappedInput.mapLabels("\xC2\xAB Back", "Generate", "Up", "Down");
  } else if (state == RUNNING) {
    renderer.text.centered(font, contentTop + 74,
                           thumbnailPhase ? "GENERATING THUMBNAILS" : "GENERATING METADATA", true,
                           EpdFontFamily::BOLD);
    renderer.text.centered(font, contentTop + 116,
                           thumbnailPhase ? "Scanning books for missing covers" : "Building grouped library indexes",
                           true, EpdFontFamily::REGULAR);
    char progress[64];
    if (totalCount > 0) {
      std::snprintf(progress, sizeof(progress), "Processed %d of %d books", static_cast<int>(processedCount),
                    static_cast<int>(totalCount));
    } else {
      std::snprintf(progress, sizeof(progress), "Processed %d books", static_cast<int>(processedCount));
    }
    renderer.text.centered(font, contentTop + 158, progress, true, EpdFontFamily::REGULAR);
    const int barWidth = std::min(320, screenWidth - 80);
    const int barX = (screenWidth - barWidth) / 2;
    const int barY = contentTop + 192;
    renderer.rectangle.render(barX, barY, barWidth, 8, true);
    renderer.rectangle.fill(barX + 1, barY + 1, std::max(1, barWidth - 2), 6, false);
    if (totalCount > 0) {
      const int fill = std::min(barWidth - 2, (barWidth - 2) * processedCount / totalCount);
      if (fill > 0) renderer.rectangle.fill(barX + 1, barY + 1, fill, 6, true);
    } else if (thumbnailPhase) {
      const int fillWidth = std::max(24, (barWidth - 2) / 4);
      const int travel = std::max(1, barWidth - 2 - fillWidth);
      const int fillX = (processedCount * 17) % travel;
      renderer.rectangle.fill(barX + 1 + fillX, barY + 1, fillWidth, 6, true);
    }
    if (currentPath[0]) {
      const std::string current = renderer.text.truncate(font, currentPath, screenWidth - 40, EpdFontFamily::REGULAR);
      renderer.text.centered(font, barY + 28, current.c_str(), true, EpdFontFamily::REGULAR);
    }
    Button::render(renderer, actionBounds(renderer), "Stop", true, font);
    mappedInput.mapLabels("Stop", "", "", "");
  } else {
    const bool success = state == SUCCESS;
    const bool cancelled = state == CANCELLED;
    renderer.text.centered(font, contentTop + 106,
                           success ? "Metadata ready" : (cancelled ? "Generation stopped" : "Generation failed"),
                           true, EpdFontFamily::BOLD);
    char progress[64];
    std::snprintf(progress, sizeof(progress), "Processed %d books", static_cast<int>(processedCount));
    renderer.text.centered(font, contentTop + 148, progress, true, EpdFontFamily::REGULAR);
    renderer.text.centered(font, contentTop + 188,
                           success && completionSummary[0] ? completionSummary : "Tap Back to return", true,
                           EpdFontFamily::REGULAR);
    mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
  }
  renderer.displayBuffer();
  if (renderingMutex) xSemaphoreGive(renderingMutex);
}

void MetadataGeneratorActivity::loop() {
  if (state == RUNNING) {
    if (SubPage::closeInput(renderer, mappedInput, [this] { cancelRequested = true; })) return;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      cancelRequested = true;
      updateRequired = true;
    }
    if (mappedInput.hasTouch()) {
      float nx = 0.0f;
      float ny = 0.0f;
      if (mappedInput.wasTouchTapInScreen(renderer, nx, ny) &&
          contains(actionBounds(renderer), static_cast<int>(nx * renderer.getScreenWidth()),
                   static_cast<int>(ny * renderer.getScreenHeight()))) {
        cancelRequested = true;
        updateRequired = true;
      }
    }
  } else {
    if (SubPage::closeInput(renderer, mappedInput, goBack)) return;
    if (state == READY && mappedInput.hasTouch() &&
        (mappedInput.wasTouchSwipeUp() || mappedInput.wasTouchSwipeDown())) {
      const int maxScroll = std::max(0, kOptionCount - optionListRows);
      const int pageStep = std::max(1, optionListRows - 1);
      const int nextOffset = std::max(0, std::min(maxScroll, scrollOffset +
          (mappedInput.wasTouchSwipeUp() ? pageStep : -pageStep)));
      if (nextOffset != scrollOffset) {
        scrollOffset = nextOffset;
        updateRequired = true;
      }
    }
    if (mappedInput.hasTouch()) {
      float nx = 0.0f;
      float ny = 0.0f;
      if (mappedInput.wasTouchTapInScreen(renderer, nx, ny)) {
        const int x = static_cast<int>(nx * renderer.getScreenWidth());
        const int y = static_cast<int>(ny * renderer.getScreenHeight());
        if (state == READY) {
          const int listBottom = optionListTop + optionListRows * kRowHeight;
          if (y >= optionListTop && y < listBottom) {
            const int row = (y - optionListTop) / kRowHeight;
            const int option = scrollOffset + row;
            if (option < kOptionCount) toggleOption(option);
          } else if (contains(actionBounds(renderer), x, y)) {
            startGeneration();
          }
        } else {
          goBack();
        }
        updateRequired = true;
        return;
      }
    }

    if (state == READY) {
      const int maxScroll = std::max(0, kOptionCount - optionListRows);
      if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        if (scrollOffset > 0) {
          --scrollOffset;
          updateRequired = true;
        }
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        if (scrollOffset < maxScroll) {
          ++scrollOffset;
          updateRequired = true;
        }
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) startGeneration();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
               mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      goBack();
    }
  }

  if (updateRequired) {
    updateRequired = false;
    render();
  }
}

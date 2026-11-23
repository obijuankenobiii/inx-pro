#include "MetadataGeneratorActivity.h"
#include "SettingsListLayout.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Button.h"
#include "activity/page/components/widget/BaseCarousel.h"
#include "activity/settings/ReaderFontSettingsDraw.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiLayout.h"
#include "util/MetadataIndex.h"
#include "util/ThumbnailGeneration.h"

namespace {
constexpr int kOptionCount = 12;
constexpr int kRowHeight = SettingsListLayout::kRowHeight;
constexpr int kButtonWidth = 180;
constexpr int kButtonSideMargin = 20;
constexpr int kButtonBottomMargin = 20;
constexpr int kCardPadding = 20;
constexpr const char* kOptionLabels[kOptionCount] = {
    "Thumbnails", "Authors", "Series", "Tags", "Publisher", "Language", "Rating", "Publication date",
    "Identifiers", "Title sort", "Author sort", "Description, dates, UUID + custom fields"};

ButtonBounds actionBounds(const GfxRenderer& renderer) {
  return {renderer.getScreenWidth() - kButtonSideMargin - kButtonWidth,
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
  failedBookCount = 0;
  currentStage[0] = '\0';
  currentBook[0] = '\0';
  completionSummary[0] = '\0';
  processedBooks.clear();
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
  failedBookCount = 0;
  currentStage[0] = '\0';
  currentBook[0] = '\0';
  completionSummary[0] = '\0';
  processedBooks.clear();
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
  ThumbnailGeneration::Result thumbnailResult;
  bool indexed = MetadataIndex::generate(
      options,
      [this](const int current, const int total, const char* stage, const char* book) {
        if (renderingMutex) xSemaphoreTake(renderingMutex, portMAX_DELAY);
        processedCount = current;
        totalCount = total;
        if (stage) strlcpy(currentStage, stage, sizeof(currentStage));
        if (book) {
          strlcpy(currentBook, book, sizeof(currentBook));
          if (!thumbnailPhase) {
            const bool failed = stage && std::strcmp(stage, "Failed") == 0;
            auto existing = std::find_if(processedBooks.begin(), processedBooks.end(),
                                         [book](const ProcessedBook& item) { return item.path == book; });
            if (existing == processedBooks.end()) {
              processedBooks.push_back({book, failed});
              if (failed) ++failedBookCount;
            } else if (failed && !existing->failed) {
              existing->failed = true;
              ++failedBookCount;
            }
          }
        }
        updateRequired = true;
        if (renderingMutex) xSemaphoreGive(renderingMutex);
      },
      [this] { return cancelRequested; },
      [this, &thumbnailResult](const char* book) {
        if (!selectedOptions[0]) return true;
        return ThumbnailGeneration::generateOne(renderer, renderingMutex, thumbnailResult, book,
                                                [this] { return cancelRequested; });
      });
  if (selectedOptions[0]) {
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
  currentStage[0] = '\0';
  currentBook[0] = '\0';
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
  const int lineHeight = renderer.text.getLineHeight(font);
  constexpr int rowGap = 12;
  constexpr int barHeight = 8;
  const int cardHeight = UiLayout::LIST_ITEM_HEIGHT;
  const int cardX = kCardPadding;
  const int cardWidth = screenWidth - 2 * kCardPadding;
  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;

  auto renderBookCards = [&](const int listTop, const int listBottom) {
    const int rowStep = cardHeight + rowGap;
    const int visibleCards = std::max(1, (listBottom - listTop + rowGap) / rowStep);
    int cardRow = 0;
    auto drawBookCard = [&](const ProcessedBook& book) {
      if (cardRow >= visibleCards) return;
      const int cardY = listTop + cardRow * rowStep;
      BaseCarousel::renderShadow(renderer, cardX + 4, cardY + 4, cardWidth, cardHeight,
                                 HomeTheme::CarouselShadowStyle::Gray);
      renderer.rectangle.fill(cardX, cardY, cardWidth, cardHeight, false, rounded);
      renderer.rectangle.render(cardX, cardY, cardWidth, cardHeight, true, rounded);
      constexpr int statusWidth = 30;
      const int maxBookWidth = std::max(1, cardWidth - 2 * kCardPadding - (book.failed ? statusWidth : 0));
      const std::string bookText = renderer.text.truncate(font, book.path.c_str(), maxBookWidth, EpdFontFamily::REGULAR);
      const int bookTextY = cardY + (cardHeight - lineHeight) / 2;
      renderer.text.render(font, cardX + kCardPadding, bookTextY, bookText.c_str(), true, EpdFontFamily::REGULAR);
      if (book.failed) {
        renderer.text.render(font, cardX + cardWidth - kCardPadding - renderer.text.getWidth(font, "X"), bookTextY,
                             "X", true, EpdFontFamily::BOLD);
      }
      ++cardRow;
    };

    bool skippedCurrent = false;
    if (currentBook[0]) {
      const auto current = std::find_if(processedBooks.begin(), processedBooks.end(),
                                        [this](const ProcessedBook& item) { return item.path == currentBook; });
      drawBookCard(current == processedBooks.end() ? ProcessedBook{currentBook, false} : *current);
    }
    for (auto it = processedBooks.rbegin(); it != processedBooks.rend() && cardRow < visibleCards; ++it) {
      if (currentBook[0] && !skippedCurrent && it->path == currentBook) {
        skippedCurrent = true;
        continue;
      }
      drawBookCard(*it);
    }
    if (cardRow == 0) drawBookCard({"Preparing...", false});
  };

  if (state == READY) {
    const ButtonBounds bounds = actionBounds(renderer);
    const int listSpace = std::max(kRowHeight, bounds.y - contentTop - 8);
    optionListRows = std::max(1, std::min(kOptionCount, listSpace / kRowHeight));
    optionListTop = contentTop + std::max(0, (listSpace - optionListRows * kRowHeight) / 2);
    scrollOffset = std::max(0, std::min(scrollOffset, kOptionCount - optionListRows));
    for (int row = 0; row < optionListRows && scrollOffset + row < kOptionCount; ++row) {
      const int index = scrollOffset + row;
      const int y = optionListTop + row * kRowHeight;
      const int textY = y + (kRowHeight - renderer.text.getLineHeight(font)) / 2;
      renderer.text.render(font, 20, textY, kOptionLabels[index], true, EpdFontFamily::REGULAR);
      ReaderFontSettingsDraw::drawToggleCheckbox(renderer, screenWidth - 24, y, kRowHeight, false,
                                                 selectedOptions[index]);
      if (index + 1 < kOptionCount && row + 1 < optionListRows) {
        renderer.line.render(SettingsListLayout::kDividerInset, y + kRowHeight - 1,
                             screenWidth - SettingsListLayout::kDividerInset, y + kRowHeight - 1, true,
                             LineRender::Style::Dotted);
      }
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
    const ButtonBounds stopBounds = actionBounds(renderer);
    const int progressY = contentTop + rowGap;
    const int barY = progressY + lineHeight + rowGap;
    const int bookCardY = barY + barHeight + rowGap;
    char progress[64];
    if (totalCount > 0) {
      std::snprintf(progress, sizeof(progress), "Processed %d of %d books", static_cast<int>(processedCount),
                    static_cast<int>(totalCount));
    } else {
      std::snprintf(progress, sizeof(progress), "Processed %d books", static_cast<int>(processedCount));
    }
    const int contentLeft = kCardPadding;
    renderer.text.render(font, contentLeft, progressY, progress, true, EpdFontFamily::REGULAR);
    const int barWidth = screenWidth - 2 * kCardPadding;
    const int barX = contentLeft;
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
    const int listTop = bookCardY;
    const int listBottom = stopBounds.y - rowGap;
    renderBookCards(listTop, listBottom);
    Button::render(renderer, actionBounds(renderer), "Stop", true, font);
    mappedInput.mapLabels("Stop", "", "", "");
  } else {
    const bool cancelled = state == CANCELLED;
    if (!cancelled) {
      char completed[64];
      std::snprintf(completed, sizeof(completed), "Completed %d books, %d failed", static_cast<int>(processedCount),
                    static_cast<int>(failedBookCount));
      const int progressY = contentTop + rowGap;
      const int barY = progressY + lineHeight + rowGap;
      renderer.text.render(font, kCardPadding, progressY, completed, true, EpdFontFamily::REGULAR);
      const int barWidth = screenWidth - 2 * kCardPadding;
      renderer.rectangle.render(kCardPadding, barY, barWidth, barHeight, true);
      renderer.rectangle.fill(kCardPadding + 1, barY + 1, std::max(1, barWidth - 2), barHeight - 2, true);
      renderBookCards(barY + barHeight + rowGap, renderer.getScreenHeight() - rowGap);
    } else {
      renderer.text.centered(font, contentTop + 106, cancelled ? "Generation stopped" : "Generation failed", true,
                             EpdFontFamily::BOLD);
      char progress[64];
      std::snprintf(progress, sizeof(progress), "Processed %d books", static_cast<int>(processedCount));
      renderer.text.centered(font, contentTop + 148, progress, true, EpdFontFamily::REGULAR);
      renderer.text.centered(font, contentTop + 188, "Tap Back to return", true, EpdFontFamily::REGULAR);
    }
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

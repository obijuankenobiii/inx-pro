/**
 * @file SleepImagePickerActivity.cpp
 * @brief Definitions for SleepImagePickerActivity.
 */

#include "SleepImagePickerActivity.h"

#include <GfxRenderer.h>
#include <ImageRender.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

#include "state/SystemSetting.h"
#include "images/Close.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "ReaderFontSettingsDraw.h"
#include "util/StringUtils.h"

namespace {
constexpr int GRID_COLS = 2;
constexpr int GRID_ROWS = 3;
constexpr int GRID_ITEMS = GRID_COLS * GRID_ROWS;
constexpr int GRID_MARGIN_X = 18;
constexpr int GRID_GAP_X = 12;
constexpr int GRID_GAP_Y = 12;
constexpr int GRID_TOP = 90;
constexpr int THUMB_INSET_X = 18;
constexpr int THUMB_INSET_Y = 12;
constexpr int RANDOM_BUTTON_W = 178;
constexpr int RANDOM_BUTTON_H = 28;
constexpr int FOOTER_SIDE_PAD = 20;

}

void SleepImagePickerActivity::taskTrampoline(void* param) {
  static_cast<SleepImagePickerActivity*>(param)->displayTaskLoop();
}

void SleepImagePickerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();
  freeGridBuffer();
  rebuildRows();

  randomEnabled = SETTINGS.sleepCustomBmp[0] == '\0';
  selectedIndex = 0;
  for (size_t i = 0; i < rows.size(); i++) {
    if (rows[i].value == SETTINGS.sleepCustomBmp) {
      selectedIndex = static_cast<int>(i);
      break;
    }
  }

  renderedPageStart = -1;
  requestRedraw();

  xTaskCreatePinnedToCore(&SleepImagePickerActivity::taskTrampoline, "SleepImagePickerTask", 12288, this, 1,
                          &displayTaskHandle, 1);
}

void SleepImagePickerActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      if (renderingMutex && xSemaphoreTake(renderingMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        render();
        xSemaphoreGive(renderingMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void SleepImagePickerActivity::rebuildRows() {
  rows.clear();

  std::vector<Row> folderImages;
  auto dir = SdMan.open("/sleep");
  if (dir && dir.isDirectory()) {
    char name[256];
    while (auto file = dir.openNextFile()) {
      file.getName(name, sizeof(name));
      std::string filename = name;
      const bool supported = StringUtils::checkFileExtension(filename, ".bmp") ||
                             StringUtils::checkFileExtension(filename, ".jpg") ||
                             StringUtils::checkFileExtension(filename, ".jpeg");
      if (filename[0] != '.' && supported) {
        folderImages.push_back({filename, filename, std::string("/sleep/") + filename});
      }
      file.close();
      yield();
    }
    dir.close();
  }

  std::sort(folderImages.begin(), folderImages.end(), [](const Row& a, const Row& b) { return a.label < b.label; });
  rows.insert(rows.end(), folderImages.begin(), folderImages.end());

  if (SdMan.exists("/sleep.bmp")) {
    rows.push_back({"sleep.bmp (SD root)", "/sleep.bmp", "/sleep.bmp"});
  }
  if (SdMan.exists("/sleep.jpg")) {
    rows.push_back({"sleep.jpg (SD root)", "/sleep.jpg", "/sleep.jpg"});
  }
  if (SdMan.exists("/sleep.jpeg")) {
    rows.push_back({"sleep.jpeg (SD root)", "/sleep.jpeg", "/sleep.jpeg"});
  }
}

void SleepImagePickerActivity::onExit() {
  renderedPageStart = -1;
  if (renderingMutex) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
  }
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  if (renderingMutex) {
    xSemaphoreGive(renderingMutex);
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }
  freeGridBuffer();
  std::vector<Row>().swap(rows);
  ActivityWithSubactivity::onExit();
}

int SleepImagePickerActivity::pageStartForIndex(const int index) const {
  if (index <= 0) {
    return 0;
  }
  return (index / GRID_ITEMS) * GRID_ITEMS;
}

int SleepImagePickerActivity::indexForSlot(const int pageStart, const int slot) const { return pageStart + slot; }

int SleepImagePickerActivity::slotForIndex(const int pageStart, const int index) const {
  const int offset = index - pageStart;
  if (offset < 0 || offset >= GRID_ITEMS) {
    return -1;
  }
  return offset;
}

void SleepImagePickerActivity::drawPickerChrome(const int pageStart, const int rowCount, const bool hasImages,
                                                const bool localRandomEnabled, const bool drawCells) {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.text.render(MONTSERRAT_16_FONT_ID, 20, 20, "Choose sleep image", true,
                       EpdFontFamily::BOLD);
  renderer.bitmap.icon(Close, pageWidth - 60, 20, 40, 40);

  const int buttonX = pageWidth - RANDOM_BUTTON_W - FOOTER_SIDE_PAD;
  const int buttonY = pageHeight - 76;
  const int gridBottom = buttonY - 14;
  const int gridHeight = std::max(1, gridBottom - GRID_TOP);
  const int cellW = (pageWidth - GRID_MARGIN_X * 2 - GRID_GAP_X) / GRID_COLS;
  const int cellH = (gridHeight - GRID_GAP_Y * (GRID_ROWS - 1)) / GRID_ROWS;

  if (hasImages && drawCells) {
    for (int slot = 0; slot < GRID_ITEMS; ++slot) {
      const int rowIndex = indexForSlot(pageStart, slot);
      const int col = slot % GRID_COLS;
      const int gridRow = slot / GRID_COLS;
      const int cellX = GRID_MARGIN_X + col * (cellW + GRID_GAP_X);
      const int cellY = GRID_TOP + gridRow * (cellH + GRID_GAP_Y);

      if (rowIndex >= rowCount) {
        continue;
      }

      renderer.rectangle.fill(cellX, cellY, cellW, cellH, false);
      renderer.rectangle.render(cellX, cellY, cellW, cellH, true);
    }
  } else if (!hasImages) {
    const int emptyX = GRID_MARGIN_X;
    const int emptyY = GRID_TOP;
    const int emptyW = pageWidth - GRID_MARGIN_X * 2;
    const int emptyH = gridBottom - GRID_TOP;
    renderer.rectangle.render(emptyX, emptyY, emptyW, emptyH, true);
    const char* msg = "No sleep images";
    const int msgFont = MONTSERRAT_12_FONT_ID;
    const int msgW = renderer.text.getWidth(msgFont, msg);
    renderer.text.render(msgFont, emptyX + (emptyW - msgW) / 2,
                         emptyY + (emptyH - renderer.text.getLineHeight(msgFont)) / 2, msg, true);
  }

  if (hasImages) {
    const int totalPages = std::max(1, (rowCount + GRID_ITEMS - 1) / GRID_ITEMS);
    const int currentPage = std::min(totalPages, pageStart / GRID_ITEMS + 1);
    char pageText[16];
    std::snprintf(pageText, sizeof(pageText), "%d - %d", currentPage, totalPages);

    const int pageFont = MONTSERRAT_12_FONT_ID;
    const int pagePadX = 8;
    const int pageTextW = renderer.text.getWidth(pageFont, pageText);
    const int pageLineH = renderer.text.getLineHeight(pageFont);
    const int pageTagH = pageLineH + 6;
    const int pageTagW = pageTextW + pagePadX * 2;
    const int pageTagX = FOOTER_SIDE_PAD;
    const int pageTagY = buttonY + (RANDOM_BUTTON_H - pageTagH) / 2;
    renderer.rectangle.fill(pageTagX, pageTagY, pageTagW, pageTagH, true, true);
    renderer.text.render(pageFont, pageTagX + pagePadX, pageTagY + (pageTagH - pageLineH) / 2, pageText, false,
                         EpdFontFamily::REGULAR);
  }

  const char* buttonText = "Random";
  const int buttonTextX = buttonX;
  const int buttonTextY =
      buttonY + (RANDOM_BUTTON_H - renderer.text.getLineHeight(MONTSERRAT_12_FONT_ID)) / 2;
  renderer.text.render(MONTSERRAT_12_FONT_ID, buttonTextX, buttonTextY, buttonText, true,
                       EpdFontFamily::BOLD);
  ReaderFontSettingsDraw::drawToggleCheckbox(renderer, pageWidth - FOOTER_SIDE_PAD, buttonY,
                                               RANDOM_BUTTON_H, false, localRandomEnabled);

  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Select", "Random", "Next");
}

void SleepImagePickerActivity::drawPickerThumbnails(const int pageStart, const int rowCount) {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int buttonY = pageHeight - 76;
  const int gridBottom = buttonY - 14;
  const int gridHeight = std::max(1, gridBottom - GRID_TOP);
  const int cellW = (pageWidth - GRID_MARGIN_X * 2 - GRID_GAP_X) / GRID_COLS;
  const int cellH = (gridHeight - GRID_GAP_Y * (GRID_ROWS - 1)) / GRID_ROWS;

  for (int slot = 0; slot < GRID_ITEMS; ++slot) {
    const int rowIndex = indexForSlot(pageStart, slot);
    if (rowIndex >= rowCount) {
      continue;
    }

    const int col = slot % GRID_COLS;
    const int gridRow = slot / GRID_COLS;
    const int cellX = GRID_MARGIN_X + col * (cellW + GRID_GAP_X);
    const int cellY = GRID_TOP + gridRow * (cellH + GRID_GAP_Y);

    bool rendered = false;
    const Row& row = rows[static_cast<size_t>(rowIndex)];
    if (!row.previewPath.empty()) {
      ImageRender::Options options;
      options.mode = ImageRenderMode::OneBit;
      options.cropToFill = false;
      options.useDisplayCache = true;
      options.asyncDisplayCache = true;
      const int thumbX = cellX + THUMB_INSET_X;
      const int thumbY = cellY + THUMB_INSET_Y;
      const int thumbW = std::max(8, cellW - THUMB_INSET_X * 2);
      const int thumbH = std::max(8, cellH - THUMB_INSET_Y * 2);
      rendered = ImageRender::create(renderer, row.previewPath).render(thumbX, thumbY, thumbW, thumbH, options);
    }

    if (!rendered) {
      const char* msg = "No preview";
    const int msgFont = MONTSERRAT_12_FONT_ID;
      const int msgW = renderer.text.getWidth(msgFont, msg);
      renderer.text.render(msgFont, cellX + (cellW - msgW) / 2,
                           cellY + (cellH - renderer.text.getLineHeight(msgFont)) / 2, msg, true);
    }

    renderer.rectangle.render(cellX, cellY, cellW, cellH, true);
    yield();
  }
}

void SleepImagePickerActivity::drawSelectionFrame(const int pageStart, const int rowCount, const int index) {
  if (index < 0 || index >= rowCount) {
    return;
  }
  const int slot = slotForIndex(pageStart, index);
  if (slot < 0) {
    return;
  }
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int buttonY = pageHeight - 76;
  const int gridBottom = buttonY - 14;
  const int gridHeight = std::max(1, gridBottom - GRID_TOP);
  const int cellW = (pageWidth - GRID_MARGIN_X * 2 - GRID_GAP_X) / GRID_COLS;
  const int cellH = (gridHeight - GRID_GAP_Y * (GRID_ROWS - 1)) / GRID_ROWS;
  const int col = slot % GRID_COLS;
  const int gridRow = slot / GRID_COLS;
  const int cellX = GRID_MARGIN_X + col * (cellW + GRID_GAP_X);
  const int cellY = GRID_TOP + gridRow * (cellH + GRID_GAP_Y);
  renderer.rectangle.render(cellX + 1, cellY + 1, cellW - 2, cellH - 2, true);
  renderer.rectangle.render(cellX + 2, cellY + 2, cellW - 4, cellH - 4, true);
}

bool SleepImagePickerActivity::storeGridBuffer(const int pageStart) {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  freeGridBuffer();

  const size_t bufferSize = renderer.getBufferSize();
  gridBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!gridBuffer) {
    return false;
  }

  memcpy(gridBuffer, frameBuffer, bufferSize);
  gridBufferStored = true;
  gridBufferPageStart = pageStart;
  return true;
}

bool SleepImagePickerActivity::restoreGridBuffer(const int pageStart) {
  if (!gridBufferStored || !gridBuffer || gridBufferPageStart != pageStart) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = renderer.getBufferSize();
  memcpy(frameBuffer, gridBuffer, bufferSize);
  return true;
}

void SleepImagePickerActivity::freeGridBuffer() {
  if (gridBuffer) {
    free(gridBuffer);
    gridBuffer = nullptr;
  }
  gridBufferStored = false;
  gridBufferPageStart = -1;
}

void SleepImagePickerActivity::render() {
  const int rowCount = static_cast<int>(rows.size());
  if (selectedIndex < 0) {
    selectedIndex = 0;
  }
  if (rowCount > 0 && selectedIndex >= rowCount) {
    selectedIndex = rowCount - 1;
  }
  const bool hasImages = rowCount > 0;
  const int pageStart = hasImages ? pageStartForIndex(selectedIndex) : 0;
  const bool lazyFirstPass = hasImages && renderedPageStart != pageStart;
  const bool pageNeedsHalfRefresh = hasImages && renderedPageStart != pageStart;

  if (hasImages && restoreGridBuffer(pageStart)) {
    drawPickerChrome(pageStart, rowCount, hasImages, randomEnabled, false);
    drawSelectionFrame(pageStart, rowCount, selectedIndex);
    renderer.displayBuffer(pageNeedsHalfRefresh ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
    renderedPageStart = pageStart;
    return;
  }

  renderer.clearScreen();
  drawPickerChrome(pageStart, rowCount, hasImages, randomEnabled);
  if (lazyFirstPass || !hasImages) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }

  if (hasImages) {
    renderer.clearScreen();
    drawPickerChrome(pageStart, rowCount, hasImages, randomEnabled);
    drawPickerThumbnails(pageStart, rowCount);
    storeGridBuffer(pageStart);
    drawSelectionFrame(pageStart, rowCount, selectedIndex);
    renderer.displayBuffer(pageNeedsHalfRefresh ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
    renderedPageStart = pageStart;
  }
}

void SleepImagePickerActivity::applySelection() {
  if (randomEnabled) {
    SETTINGS.setSleepCustomBmpFromInput("");
    SETTINGS.saveToFile();
    onBack();
    return;
  }
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(rows.size())) {
    return;
  }
  const std::string& v = rows[static_cast<size_t>(selectedIndex)].value;
  SETTINGS.setSleepCustomBmpFromInput(v.c_str());
  SETTINGS.saveToFile();
  onBack();
}

bool SleepImagePickerActivity::paginate(const int direction) {
  if (rows.empty() || direction == 0) {
    return false;
  }

  const int rowCount = static_cast<int>(rows.size());
  const int currentStart = pageStartForIndex(selectedIndex);
  const int pageCount = std::max(1, (rowCount + GRID_ITEMS - 1) / GRID_ITEMS);
  const int currentPage = currentStart / GRID_ITEMS;
  const int targetPage = std::max(0, std::min(pageCount - 1, currentPage + (direction > 0 ? 1 : -1)));
  const int targetStart = targetPage * GRID_ITEMS;

  if (targetStart == currentStart) {
    return false;
  }

  const int currentSlot = std::max(0, selectedIndex - currentStart);
  selectedIndex = std::min(targetStart + currentSlot, rowCount - 1);
  renderedPageStart = -1;
  requestRedraw();
  return true;
}

void SleepImagePickerActivity::requestRedraw() {
  if (!rows.empty()) {
    if (selectedIndex < 0) {
      selectedIndex = 0;
    } else if (selectedIndex >= static_cast<int>(rows.size())) {
      selectedIndex = static_cast<int>(rows.size()) - 1;
    }
  } else {
    selectedIndex = 0;
  }
  updateRequired = true;
}

void SleepImagePickerActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (mappedInput.hasTouch()) {
    if (mappedInput.wasTouchSwipeUp()) {
      onBack();
      return;
    }
    if (mappedInput.wasTouchSwipeRight()) {
      if (paginate(1)) {
        return;
      }
    } else if (mappedInput.wasTouchSwipeLeft()) {
      if (paginate(-1)) {
        return;
      }
    }

    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int screenWidth = renderer.getScreenWidth();
      const int screenHeight = renderer.getScreenHeight();
      const int tapX = static_cast<int>(tapNx * screenWidth);
      const int tapY = static_cast<int>(tapNy * screenHeight);
      if (tapX >= screenWidth - 60 && tapX < screenWidth - 20 && tapY >= 20 && tapY < 60) {
        onBack();
        return;
      }
      const int buttonY = screenHeight - 76;
      const int buttonX = screenWidth - RANDOM_BUTTON_W - FOOTER_SIDE_PAD;
      if (tapX >= buttonX && tapX < buttonX + RANDOM_BUTTON_W && tapY >= buttonY &&
          tapY < buttonY + RANDOM_BUTTON_H) {
        randomEnabled = !randomEnabled;
        SETTINGS.setSleepCustomBmpFromInput(
            randomEnabled ? "" : (rows.empty() ? "" : rows[static_cast<size_t>(selectedIndex)].value.c_str()));
        SETTINGS.saveToFile();
        renderedPageStart = -1;
        freeGridBuffer();
        requestRedraw();
        return;
      }

      if (!rows.empty() && tapY >= GRID_TOP && tapY < buttonY - 14) {
        const int pageStart = pageStartForIndex(selectedIndex);
        const int gridHeight = std::max(1, (buttonY - 14) - GRID_TOP);
        const int cellW = (screenWidth - GRID_MARGIN_X * 2 - GRID_GAP_X) / GRID_COLS;
        const int cellH = (gridHeight - GRID_GAP_Y * (GRID_ROWS - 1)) / GRID_ROWS;
        const int col = (tapX - GRID_MARGIN_X) / (cellW + GRID_GAP_X);
        const int gridRow = (tapY - GRID_TOP) / (cellH + GRID_GAP_Y);
        if (col >= 0 && col < GRID_COLS && gridRow >= 0 && gridRow < GRID_ROWS) {
          const int cellX = GRID_MARGIN_X + col * (cellW + GRID_GAP_X);
          const int cellY = GRID_TOP + gridRow * (cellH + GRID_GAP_Y);
          if (tapX >= cellX && tapX < cellX + cellW && tapY >= cellY && tapY < cellY + cellH) {
            const int tappedIndex = pageStart + gridRow * GRID_COLS + col;
            if (tappedIndex >= 0 && tappedIndex < static_cast<int>(rows.size())) {
              selectedIndex = tappedIndex;
              randomEnabled = false;
              applySelection();
              return;
            }
          }
        }
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    randomEnabled = false;
    applySelection();
    return;
  }

  bool needRedraw = false;

  const bool randomPressed = mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool upPressed = mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool downPressed = mappedInput.wasPressed(MappedInputManager::Button::Down);
  const bool nextPressed = mappedInput.wasPressed(MappedInputManager::Button::Right);

  if (randomPressed) {
    randomEnabled = !randomEnabled;
    SETTINGS.setSleepCustomBmpFromInput(
        randomEnabled ? "" : (rows.empty() ? "" : rows[static_cast<size_t>(selectedIndex)].value.c_str()));
    SETTINGS.saveToFile();
    renderedPageStart = -1;
    freeGridBuffer();
    needRedraw = true;
  } else if (!rows.empty() && (upPressed || downPressed || nextPressed)) {
    const int count = static_cast<int>(rows.size());
    if (upPressed) {
      selectedIndex = (selectedIndex + count - 1) % count;
    } else {
      selectedIndex = (selectedIndex + 1) % count;
    }
    needRedraw = true;
  }

  if (needRedraw) {
    requestRedraw();
  }
}

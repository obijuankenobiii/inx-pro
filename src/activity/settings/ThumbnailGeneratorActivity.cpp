/**
 * @file ThumbnailGeneratorActivity.cpp
 * @brief Definitions for ThumbnailGeneratorActivity.
 */

#include "ThumbnailGeneratorActivity.h"

#include <Epub.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <ImageRender.h>
#include <Pdf.h>
#include <SDCardManager.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Button.h"
#include "activity/page/views/Library/Thumb.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/StringUtils.h"

namespace {
constexpr uint32_t kDisplayTaskStack = 4096;
// Sized for the deepest processBook() path: PDF thumbnail generation runs a recursive object-graph parser
// plus a content-stream interpreter (several layers of std::string/std::vector/std::function locals per
// page), noticeably heavier than Epub/Xtc's shallower raster-cover extraction.
constexpr uint32_t kWorkerTaskStack = 16384;
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

// Pre-populate the on-disk display cache for a freshly-generated thumbnail at the exact size
// Library's 2x2 grid draws covers at, so the library's first render after "Generate
// Thumbnails" hits the cache (raw read) instead of paying for a fresh decode+dither per book.
void precacheShelfThumbnail(GfxRenderer& renderer, const std::string& thumbPath) {
  int coverW = 0;
  int coverH = 0;
  views::library::Thumb::getThumbnailSize(renderer, coverW, coverH);
  if (coverW <= 2 || coverH <= 2) {
    return;
  }
  ImageRender::Options options;
  // Must match Library's cover options exactly (cropToFill included) - the display
  // cache is keyed on these, so a mismatch here means the shelf render misses this cache entry.
  options.cropToFill = true;
  options.useDisplayCache = true;
  // This helper renders into the shared framebuffer only to populate the on-disk cache. Clear the
  // destination first: image renderers intentionally paint only ink pixels, so stale pixels from a
  // previous cover would otherwise be persisted as part of this thumbnail's cache.
  renderer.rectangle.fill(0, 0, coverW - 2, coverH - 2, false);
  ImageRender::create(renderer, thumbPath).render(0, 0, coverW - 2, coverH - 2, options);
}

void drawThinProgressBar(const GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                         const int fillX, const int fillW) {
  renderer.rectangle.render(x, y, w, h, true);
  const int innerW = std::max(1, w - 2);
  renderer.rectangle.fill(x + 1, y + 1, innerW, h - 2, false);
  if (fillW > 0) {
    const int clampedX = std::max(0, std::min(innerW, fillX));
    const int clampedW = std::max(0, std::min(innerW - clampedX, fillW));
    if (clampedW > 0) {
      renderer.rectangle.fill(x + 1 + clampedX, y + 1, clampedW, h - 2, true);
    }
  }
}

void drawThumbnailProgressView(const GfxRenderer& renderer, const int pageWidth, const int screenHeight,
                               const bool running, const bool success, const bool cancelled, const int processedCount,
                               const int generatedCount, const int skippedCount, const int failedCount,
                               const char* currentPath) {
  const int contentTop = SubPage::header(renderer, "Thumbnails");
  const int centerY = contentTop + (screenHeight - contentTop - 80) / 2;

  const char* eyebrow = running     ? "GENERATING THUMBNAILS"
                        : success   ? "THUMBNAILS READY"
                        : cancelled ? "GENERATION STOPPED"
                                    : "GENERATION FAILED";
  const char* title = running     ? "Scanning library"
                      : success   ? "Thumbnails complete"
                      : cancelled ? "Stopped"
                                  : "Thumbnail generation failed";
  renderer.text.centered(systemFontId(), centerY - 92, eyebrow, true, EpdFontFamily::BOLD);
  renderer.text.centered(systemFontId(), centerY - 54, title, true, EpdFontFamily::BOLD);

  char line[80];
  snprintf(line, sizeof(line), "Processed %d books", processedCount);
  renderer.text.centered(systemFontId(), centerY - 10, line, true, EpdFontFamily::REGULAR);

  const int barW = std::min(300, pageWidth - 72);
  constexpr int barH = 6;
  const int barX = (pageWidth - barW) / 2;
  const int barY = centerY + 28;
  const int innerW = std::max(1, barW - 2);
  int fillX = 0;
  int fillW = 0;
  if (running) {
    fillW = std::max(36, innerW / 4);
    const int travel = std::max(1, innerW - fillW);
    fillX = (processedCount * 17) % travel;
  } else if (success) {
    fillW = innerW;
  }
  drawThinProgressBar(renderer, barX, barY, barW, barH, fillX, fillW);

  snprintf(line, sizeof(line), "Generated %d   Skipped %d   Failed %d", generatedCount, skippedCount, failedCount);
  renderer.text.centered(systemFontId(), barY + 26, line, true, EpdFontFamily::REGULAR);

  if (running && currentPath && currentPath[0] != '\0') {
    const std::string path = renderer.text.truncate(systemFontId(), currentPath, pageWidth - 60);
    renderer.text.centered(systemFontId(), barY + 54, path.c_str(), true, EpdFontFamily::REGULAR);
  }
}
}  // namespace

void ThumbnailGeneratorActivity::displayTaskTrampoline(void* param) {
  static_cast<ThumbnailGeneratorActivity*>(param)->displayTaskLoop();
}

void ThumbnailGeneratorActivity::workerTaskTrampoline(void* param) {
  static_cast<ThumbnailGeneratorActivity*>(param)->workerTaskLoop();
}

void ThumbnailGeneratorActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();
  updateRequired = true;
  cancelRequested = false;
  state = READY;
  processedCount = 0;
  generatedCount = 0;
  skippedCount = 0;
  failedCount = 0;
  currentPath[0] = '\0';

  xTaskCreatePinnedToCore(&ThumbnailGeneratorActivity::displayTaskTrampoline, "ThumbGenDisplayTask", kDisplayTaskStack,
                          this, 1, &displayTaskHandle, 1);
}

void ThumbnailGeneratorActivity::onExit() {
  cancelRequested = true;

  const unsigned long start = millis();
  while (workerTaskHandle && millis() - start < 1500) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  ActivityWithSubactivity::onExit();

  if (workerTaskHandle) {
    vTaskDelete(workerTaskHandle);
    workerTaskHandle = nullptr;
  }
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  if (renderingMutex) {
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }
}

void ThumbnailGeneratorActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      if (renderingMutex && xSemaphoreTake(renderingMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        render();
        xSemaphoreGive(renderingMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void ThumbnailGeneratorActivity::startGeneration() {
  if (state == RUNNING || workerTaskHandle != nullptr) {
    return;
  }

  cancelRequested = false;
  state = RUNNING;
  processedCount = 0;
  generatedCount = 0;
  skippedCount = 0;
  failedCount = 0;
  currentPath[0] = '\0';
  updateRequired = true;

  // Keep EPUB/JPEG decoding on core 0 while the display task remains on core 1.
  xTaskCreatePinnedToCore(&ThumbnailGeneratorActivity::workerTaskTrampoline, "ThumbGenWorkerTask", kWorkerTaskStack,
                          this, 1, &workerTaskHandle, 0);
}

bool ThumbnailGeneratorActivity::shouldSkipPath(const char* name) const {
  return name[0] == '.' || strcmp(name, "System Volume Information") == 0 || strcmp(name, ".metadata") == 0 ||
         strcmp(name, "sleep") == 0;
}

bool ThumbnailGeneratorActivity::isSupportedBookFile(const std::string& filename) const {
  return StringUtils::checkFileExtension(filename, ".epub") || StringUtils::checkFileExtension(filename, ".xtc") ||
         StringUtils::checkFileExtension(filename, ".pdf");
}

bool ThumbnailGeneratorActivity::processBook(const std::string& path) {
  strlcpy(currentPath, path.c_str(), sizeof(currentPath));
  updateRequired = true;

  INX_SERIAL.printf("[%lu] [THUMB-DEBUG] start path=%s\n", millis(), path.c_str());

  if (StringUtils::checkFileExtension(path, ".epub")) {
    Epub epub(path, "/.metadata/epub");
    const std::string thumbJpegPath = epub.getThumbJpegPath();
    const std::string thumbPngPath = epub.getThumbPngPath();
    const std::string thumbBmpPath = epub.getThumbBmpPath();
    const bool hasThumbJpeg = SdMan.exists(thumbJpegPath.c_str());
    const bool hasThumbPng = SdMan.exists(thumbPngPath.c_str());
    const bool hasThumbBmp = SdMan.exists(thumbBmpPath.c_str());
    if (hasThumbJpeg || hasThumbPng || hasThumbBmp) {
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] skip existing epub thumbnail jpg=%d png=%d bmp=%d cache=%s\n", millis(),
                    hasThumbJpeg, hasThumbPng, hasThumbBmp, epub.getCachePath().c_str());
      skippedCount++;
      processedCount++;
      return true;
    }

    if (!epub.load()) {
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB load failed path=%s cache=%s\n", millis(), path.c_str(),
                    epub.getCachePath().c_str());
      processedCount++;
      failedCount++;
      return false;
    }

    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB loaded title=\"%s\" cover=%s\n", millis(), epub.getTitle().c_str(),
                  epub.getCoverItemHref().c_str());
    const bool ok = epub.generateThumbBmp();
    processedCount++;
    if (ok) {
      generatedCount++;
      const bool outputExists = SdMan.exists(thumbJpegPath.c_str()) || SdMan.exists(thumbPngPath.c_str()) ||
                                 SdMan.exists(thumbBmpPath.c_str());
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB generation returned ok output=%d jpg=%d png=%d bmp=%d\n", millis(),
                    outputExists, SdMan.exists(thumbJpegPath.c_str()), SdMan.exists(thumbPngPath.c_str()),
                    SdMan.exists(thumbBmpPath.c_str()));
      // precacheShelfThumbnail draws into the shared framebuffer (it's just borrowing ImageRender's
      // decode-then-store side effect) - take the same mutex displayTaskLoop uses before render(), so
      // the two don't interleave writes to the same buffer or race a displayBuffer() refresh.
      if (renderingMutex && xSemaphoreTake(renderingMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        bool precacheOk = false;
        if (SdMan.exists(thumbJpegPath.c_str())) {
          precacheShelfThumbnail(renderer, thumbJpegPath);
          precacheOk = true;
        } else if (SdMan.exists(thumbPngPath.c_str())) {
          precacheShelfThumbnail(renderer, thumbPngPath);
          precacheOk = true;
        } else if (SdMan.exists(thumbBmpPath.c_str())) {
          precacheShelfThumbnail(renderer, thumbBmpPath);
          precacheOk = true;
        }
        INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB display-cache prewarm source=%d\n", millis(), precacheOk);
        xSemaphoreGive(renderingMutex);
      } else {
        INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB display-cache prewarm skipped: renderer mutex timeout\n", millis());
      }
    } else {
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] EPUB generation failed path=%s cover=%s\n", millis(), path.c_str(),
                    epub.getCoverItemHref().c_str());
      failedCount++;
    }
    return ok;
  }

  if (StringUtils::checkFileExtension(path, ".xtc")) {
    Xtc xtc(path, "/.metadata/xtc");
    const std::string thumbBmpPath = xtc.getThumbBmpPath();
    if (SdMan.exists(thumbBmpPath.c_str())) {
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] skip existing xtc thumbnail path=%s\n", millis(), thumbBmpPath.c_str());
      skippedCount++;
      processedCount++;
      return true;
    }
    if (!xtc.load()) {
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] XTC load failed path=%s\n", millis(), path.c_str());
      processedCount++;
      failedCount++;
      return false;
    }
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] XTC loaded path=%s\n", millis(), path.c_str());
    const bool ok = xtc.generateThumbBmp();
    processedCount++;
    if (ok) {
      generatedCount++;
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] XTC generation returned ok output=%d\n", millis(),
                    SdMan.exists(thumbBmpPath.c_str()));
      if (renderingMutex && xSemaphoreTake(renderingMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        precacheShelfThumbnail(renderer, thumbBmpPath);
        xSemaphoreGive(renderingMutex);
      } else {
        INX_SERIAL.printf("[%lu] [THUMB-DEBUG] XTC display-cache prewarm skipped: renderer mutex timeout\n", millis());
      }
    } else {
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] XTC generation failed path=%s\n", millis(), path.c_str());
      failedCount++;
    }
    return ok;
  }

  if (StringUtils::checkFileExtension(path, ".pdf")) {
    Pdf pdf(path, "/.metadata/pdf");
    const std::string thumbBmpPath = pdf.getThumbBmpPath();
    if (SdMan.exists(thumbBmpPath.c_str())) {
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] skip existing pdf thumbnail path=%s\n", millis(), thumbBmpPath.c_str());
      skippedCount++;
      processedCount++;
      return true;
    }
    if (!pdf.load()) {
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] PDF load failed path=%s\n", millis(), path.c_str());
      processedCount++;
      failedCount++;
      return false;
    }
    INX_SERIAL.printf("[%lu] [THUMB-DEBUG] PDF loaded path=%s\n", millis(), path.c_str());
    // Unlike Epub/Xtc, generateThumbBmp() draws page 0 into the live framebuffer (there's no pre-rendered
    // raster to resize), so the whole call - not just the precache step below - needs the same mutex
    // displayTaskLoop takes before render(), to avoid interleaving with this activity's own progress UI.
    bool ok = false;
    if (renderingMutex && xSemaphoreTake(renderingMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
      ok = pdf.generateThumbBmp(renderer);
      if (ok) precacheShelfThumbnail(renderer, thumbBmpPath);
      xSemaphoreGive(renderingMutex);
    } else {
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] PDF generation skipped: renderer mutex timeout\n", millis());
    }
    processedCount++;
    if (ok) {
      generatedCount++;
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] PDF generation returned ok output=%d\n", millis(),
                    SdMan.exists(thumbBmpPath.c_str()));
    } else {
      INX_SERIAL.printf("[%lu] [THUMB-DEBUG] PDF generation failed path=%s\n", millis(), path.c_str());
      failedCount++;
    }
    return ok;
  }

  return false;
}

bool ThumbnailGeneratorActivity::scanPath(const std::string& path) {
  if (cancelRequested) {
    return false;
  }

  FsFile dir = SdMan.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return true;
  }

  dir.rewindDirectory();
  char name[256];

  while (!cancelRequested) {
    FsFile file = dir.openNextFile();
    if (!file) {
      break;
    }

    file.getName(name, sizeof(name));
    if (shouldSkipPath(name)) {
      file.close();
      continue;
    }

    std::string fullPath = path;
    if (fullPath.empty()) {
      fullPath = "/";
    }
    if (fullPath.back() != '/') {
      fullPath += "/";
    }
    fullPath += name;

    if (file.isDirectory()) {
      file.close();
      if (!scanPath(fullPath)) {
        dir.close();
        return false;
      }
      continue;
    }

    file.close();
    if (!isSupportedBookFile(name)) {
      continue;
    }
    processBook(fullPath);
    if ((processedCount % 4) == 0) {
      updateRequired = true;
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  dir.close();
  return !cancelRequested;
}

void ThumbnailGeneratorActivity::workerTaskLoop() {
  scanPath("/");

  if (cancelRequested) {
    state = CANCELLED;
  } else if (failedCount > 0 && generatedCount == 0 && skippedCount == 0) {
    state = FAILED;
  } else {
    state = SUCCESS;
  }
  currentPath[0] = '\0';
  updateRequired = true;
  workerTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void ThumbnailGeneratorActivity::render() {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  if (state == READY) {
    const int contentTop = SubPage::header(renderer, "Thumbnails");
    const int centerY = contentTop + (screenHeight - contentTop - 80) / 2;
    renderer.text.centered(systemFontId(), centerY - 92, "GENERATE THUMBNAILS", true,
                           EpdFontFamily::BOLD);
    renderer.text.centered(systemFontId(), centerY - 54, "Build missing covers", true,
                           EpdFontFamily::BOLD);
    renderer.text.centered(systemFontId(), centerY - 10, "Existing thumbnails are skipped.", true,
                           EpdFontFamily::REGULAR);

    const int barW = std::min(300, pageWidth - 72);
    constexpr int barH = 6;
    const int barX = (pageWidth - barW) / 2;
    const int barY = centerY + 28;
    drawThinProgressBar(renderer, barX, barY, barW, barH, 0, 0);
    renderer.text.centered(systemFontId(), barY + 26, "Ready to scan EPUB and XTC books", true,
                           EpdFontFamily::REGULAR);

    drawActionButton(renderer, pageWidth, screenHeight, "Generate", false);
    const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Generate", "", "");
    renderer.displayBuffer();
    return;
  }

  if (state == RUNNING) {
    drawThumbnailProgressView(renderer, pageWidth, screenHeight, true, false, false, processedCount, generatedCount,
                              skippedCount, failedCount, currentPath);
    drawActionButton(renderer, pageWidth, screenHeight, "Stop", true);
    const auto labels = mappedInput.mapLabels("Stop", "", "", "");
    renderer.displayBuffer();
    return;
  }

  drawThumbnailProgressView(renderer, pageWidth, screenHeight, false, state == SUCCESS, state == CANCELLED,
                            processedCount, generatedCount, skippedCount, failedCount, nullptr);

  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
  renderer.displayBuffer();
}

void ThumbnailGeneratorActivity::loop() {
  if (SubPage::closeInput(renderer, mappedInput, goBack)) return;

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
      const ButtonBounds bounds = actionButtonBounds(renderer.getScreenWidth(), renderer.getScreenHeight());
      if (state == READY) {
        if (isInside(bounds, tapX, tapY)) {
          startGeneration();
        }
      } else if (state == RUNNING) {
        if (isInside(bounds, tapX, tapY)) {
          cancelRequested = true;
          updateRequired = true;
        }
      } else {
        goBack();
      }
      return;
    }
  }

  if (state == READY) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      startGeneration();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
      return;
    }
    return;
  }

  if (state == RUNNING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      cancelRequested = true;
      updateRequired = true;
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    goBack();
  }
}

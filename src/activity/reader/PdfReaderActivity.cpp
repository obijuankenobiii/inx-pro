/**
 * @file PdfReaderActivity.cpp
 * @brief Definitions for PdfReaderActivity.
 */

#include "PdfReaderActivity.h"

#include <esp_heap_caps.h>

#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <algorithm>
#include <limits>

#include "state/RecentBooks.h"
#include "state/BookState.h"
#include "state/ReaderSetting.h"
#include "state/Session.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
constexpr unsigned long goHomeMs = 1000;
constexpr uint32_t kPagesCacheMagic = 0x50444650u;
constexpr uint32_t kPagesCacheVersion = 5u;
constexpr uint32_t kPdfWorkerStackBytes = 16384;
constexpr int kChunkCount = 10;
constexpr unsigned long kIdleNextChunkBuildDelayMs = 1500;

TextBlock::Style toTextBlockAlignment(const uint8_t paragraphAlignment) {
  switch (paragraphAlignment) {
    case SystemSetting::CENTER_ALIGN:
      return TextBlock::CENTER_ALIGN;
    case SystemSetting::RIGHT_ALIGN:
      return TextBlock::RIGHT_ALIGN;
    case SystemSetting::JUSTIFIED:
    case SystemSetting::FOLLOW_CSS:
      return TextBlock::LEFT_ALIGN;
    default:
      return TextBlock::LEFT_ALIGN;
  }
}

struct ChunkHeader {
  bool valid = false;
  uint32_t pageCount = 0;
};

ChunkHeader readChunkHeader(FsFile& file, const int fontId, const int headerFontId, const int viewportWidth,
                            const int viewportHeight, const uint8_t align, const uint8_t extraParaSpacing,
                            const float lineCompression, const float wordSpacingFactor) {
  ChunkHeader result;
  uint32_t magic = 0, version = 0;
  int32_t fileFontId = 0, fileHeaderFontId = 0, fileVw = 0, fileVh = 0;
  uint8_t fileAlign = 0, fileExtraParaSpacing = 0;
  float fileLineCompression = 0, fileWordSpacingFactor = 0;
  serialization::readPod(file, magic);
  serialization::readPod(file, version);
  serialization::readPod(file, fileFontId);
  serialization::readPod(file, fileHeaderFontId);
  serialization::readPod(file, fileVw);
  serialization::readPod(file, fileVh);
  serialization::readPod(file, fileAlign);
  serialization::readPod(file, fileExtraParaSpacing);
  serialization::readPod(file, fileLineCompression);
  serialization::readPod(file, fileWordSpacingFactor);
  serialization::readPod(file, result.pageCount);

  result.valid = magic == kPagesCacheMagic && version == kPagesCacheVersion && fileFontId == fontId &&
                fileHeaderFontId == headerFontId && fileVw == viewportWidth && fileVh == viewportHeight &&
                fileAlign == align && fileExtraParaSpacing == extraParaSpacing &&
                fileLineCompression == lineCompression && fileWordSpacingFactor == wordSpacingFactor;
  return result;
}
}

void PdfReaderActivity::taskTrampoline(void* param) {
  auto* self = static_cast<PdfReaderActivity*>(param);
  self->displayTaskLoop();
}

void PdfReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!pdf) {
    return;
  }

  switch (READER_SETTINGS.orientation) {
    case SystemSetting::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case SystemSetting::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case SystemSetting::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case SystemSetting::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }

  mappedInput.setInvertDirectionalAxes180(renderer.getOrientation() == GfxRenderer::Orientation::LandscapeClockwise);

  renderingMutex = xSemaphoreCreateMutex();
  pdf->setupCacheDir();

  APP_STATE.lastRead = pdf->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(pdf->getPath(), pdf->getCachePath(), pdf->getTitle(), pdf->getAuthor());
  BOOK_STATE.addOrUpdateBook(pdf->getPath(), pdf->getTitle(), pdf->getAuthor());
  BOOK_STATE.setReading(pdf->getPath(), true, pdf->getTitle());

  updateRequired = true;

  taskStack = static_cast<StackType_t*>(heap_caps_malloc(kPdfWorkerStackBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!taskStack) {
    INX_SERIAL.printf("[%lu] [PDR] PSRAM stack allocation failed\n", millis());
    return;
  }
  displayTaskHandle = xTaskCreateStatic(&PdfReaderActivity::taskTrampoline, "PdfReaderActivityTask",
                                        kPdfWorkerStackBytes, this, 1, taskStack, &taskControlBlock);
  if (!displayTaskHandle) {
    INX_SERIAL.printf("[%lu] [PDR] Render task creation failed\n", millis());
    heap_caps_free(taskStack);
    taskStack = nullptr;
  }
}

void PdfReaderActivity::onExit() {
  ActivityWithSubactivity::onExit();

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  mappedInput.setInvertDirectionalAxes180(false);

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  if (taskStack) {
    heap_caps_free(taskStack);
    taskStack = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  if (pdf) {
    saveProgress();
  }
  pdf.reset();
  if (pagesFile) pagesFile.close();
  pageFileOffsets.clear();
  currentPageData.reset();

  renderer.resetTransientReaderState();

#if FREEINK_DEVICE_X4PRO
  renderer.clearScreen(0xFF);
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
#endif

  FontManager::unloadAllSDFonts();
}

void PdfReaderActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= goHomeMs) {
    onGoToHome();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && mappedInput.getHeldTime() < goHomeMs) {
    onGoBack();
    return;
  }

  if (mappedInput.hasTouch() && mappedInput.wasTouchSwipeUpForRenderer(renderer)) {
    onGoBack();
    return;
  }

  bool tapPrevTriggered = false;
  bool tapNextTriggered = false;
  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f, tapNy = 0.0f;
    if (READER_SETTINGS.pageTurnMode == ReaderSetting::PAGE_TURN_TAP &&
        mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      (tapNx < 0.5f ? tapPrevTriggered : tapNextTriggered) = true;
    }
  }

  const bool swipePrevTriggered = READER_SETTINGS.pageTurnMode == ReaderSetting::PAGE_TURN_SWIPE &&
                                  mappedInput.hasTouch() && mappedInput.wasTouchSwipeRightForRenderer(renderer);
  const bool swipeNextTriggered = READER_SETTINGS.pageTurnMode == ReaderSetting::PAGE_TURN_SWIPE &&
                                  mappedInput.hasTouch() && mappedInput.wasTouchSwipeLeftForRenderer(renderer);

  const bool usePressForPageTurn = READER_SETTINGS.longPressChapterSkip == SystemSetting::LONG_PRESS_OFF;
  const MappedInputManager::MotionGesture motionGesture = mappedInput.readMotionGesture(
      static_cast<uint8_t>(renderer.getOrientation()), SETTINGS.shakePageTurn, SETTINGS.shakePageTurnSensitivity);
  const bool prevTriggered = tapPrevTriggered || swipePrevTriggered ||
                             motionGesture == MappedInputManager::MotionGesture::Previous ||
                             (usePressForPageTurn ? (mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                                                     mappedInput.wasPressed(MappedInputManager::Button::Left))
                                                  : (mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                                                     mappedInput.wasReleased(MappedInputManager::Button::Left)));
  const bool powerPageTurn = READER_SETTINGS.btnPowerShortAction == SystemSetting::BTN_ACTION_PAGE_NEXT &&
                             mappedInput.wasReleased(MappedInputManager::Button::Power);
  const bool nextTriggered =
      tapNextTriggered || swipeNextTriggered || motionGesture == MappedInputManager::MotionGesture::Next ||
      (usePressForPageTurn ? (mappedInput.wasPressed(MappedInputManager::Button::PageForward) || powerPageTurn ||
                              mappedInput.wasPressed(MappedInputManager::Button::Right))
                           : (mappedInput.wasReleased(MappedInputManager::Button::PageForward) || powerPageTurn ||
                              mappedInput.wasReleased(MappedInputManager::Button::Right)));

  if (!prevTriggered && !nextTriggered) {
    return;
  }

  if (prevTriggered && (currentPage > 0 || currentChunk > 0)) {
    currentPage--;
    updateRequired = true;
  } else if (nextTriggered && (currentPage < totalPages - 1 || currentChunk < chunkCount - 1)) {
    currentPage++;
    updateRequired = true;
  }
}

void PdfReaderActivity::displayTaskLoop() {
  idleSinceMs = millis();
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      renderScreen();
      xSemaphoreGive(renderingMutex);
      idleSinceMs = millis();
    } else if (textReady && millis() - idleSinceMs > kIdleNextChunkBuildDelayMs) {
      serviceIdleNextChunkBuild();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void PdfReaderActivity::maybeYield() {
  if (backgroundBuildActive && updateRequired) {
    throw BuildAborted{};
  }
  const unsigned long now = millis();
  if (now - lastYieldMs > 50) {
    vTaskDelay(1);
    lastYieldMs = now;
  }
}

void PdfReaderActivity::renderBuildProgress(const char* status, const int current, const int total, const bool force) {
  const unsigned long now = millis();
  if (!force && now - lastProgressRedrawMs < 800) return;
  lastProgressRedrawMs = now;

  renderer.clearScreen();
  const int pageWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int centerY = screenHeight / 2;

  renderer.text.centered(MONTSERRAT_10_FONT_ID, centerY - 66, "PREPARING BOOK", true, EpdFontFamily::BOLD);
  const std::string title = renderer.text.truncate(MONTSERRAT_12_FONT_ID, pdf->getTitle().c_str(), pageWidth - 80);
  renderer.text.centered(MONTSERRAT_12_FONT_ID, centerY - 34, title.c_str(), true, EpdFontFamily::BOLD);
  renderer.text.centered(MONTSERRAT_10_FONT_ID, centerY - 4, status, true, EpdFontFamily::REGULAR);

  const int barW = std::min(300, pageWidth - 72);
  constexpr int barH = 6;
  const int barX = (pageWidth - barW) / 2;
  const int barY = centerY + 24;
  const int innerW = std::max(1, barW - 2);
  renderer.rectangle.render(barX, barY, barW, barH, true);
  renderer.rectangle.fill(barX + 1, barY + 1, innerW, barH - 2, false);
  if (total > 0) {
    const int fillW = static_cast<int>((static_cast<int64_t>(innerW) * std::min(current, total)) / total);
    if (fillW > 0) renderer.rectangle.fill(barX + 1, barY + 1, std::min(innerW, fillW), barH - 2, true);

    char line[32];
    snprintf(line, sizeof(line), "%d / %d", current, total);
    renderer.text.centered(MONTSERRAT_8_FONT_ID, barY + 22, line, true, EpdFontFamily::REGULAR);
  }

  renderer.displayBuffer();
}

void PdfReaderActivity::renderChunkLoadingOverlay(const int current, const int total, const bool force) {
  const unsigned long now = millis();
  if (!overlayPopupShown) {
    overlayPopupLayout = ScreenComponents::drawPopup(renderer, "Loading pages...");
    overlayPopupShown = true;
    lastProgressRedrawMs = now;
    return;
  }
  if (!force && now - lastProgressRedrawMs < 800) return;
  lastProgressRedrawMs = now;

  const int percent = total > 0 ? static_cast<int>((static_cast<int64_t>(current) * 100) / total) : 0;
  ScreenComponents::fillPopupProgress(renderer, overlayPopupLayout, percent);
}

void PdfReaderActivity::reportBuildProgress(const ChunkProgressUi ui, const char* status, const int current,
                                            const int total, const bool force) {
  switch (ui) {
    case ChunkProgressUi::None:
      return;
    case ChunkProgressUi::FullScreen:
      renderBuildProgress(status, current, total, force);
      return;
    case ChunkProgressUi::Overlay:
      renderChunkLoadingOverlay(current, total, force);
      return;
  }
}

void PdfReaderActivity::computeLayoutMetrics() {
  cachedFontId = READER_SETTINGS.getReaderFontId();
  FontManager::ensureFontReady(cachedFontId, renderer);

  const uint8_t headerSizeIndex =
      static_cast<uint8_t>(std::min<int>(READER_SETTINGS.fontSize + 1, SystemSetting::EXTRA_LARGE));
  cachedHeaderFontId = READER_SETTINGS.getReaderFontIdForFamilyAndSize(READER_SETTINGS.fontFamily, headerSizeIndex);
  FontManager::ensureFontReady(cachedHeaderFontId, renderer);

  cachedScreenMargin = READER_SETTINGS.screenMargin;
  cachedParagraphAlignment = READER_SETTINGS.paragraphAlignment;
  cachedExtraParagraphSpacing = READER_SETTINGS.extraParagraphSpacing;
  cachedLineCompression = READER_SETTINGS.getReaderLineCompression();
  cachedWordSpacingFactor = READER_SETTINGS.getReaderWordSpacingFactor();

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += cachedScreenMargin;
  orientedMarginLeft += cachedScreenMargin;
  orientedMarginRight += cachedScreenMargin;
  orientedMarginBottom += cachedScreenMargin;

  viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
}

void PdfReaderActivity::chunkPageRange(const int chunkIndex, int& startPage, int& endPage) const {
  const int pageCount = pdf->getPageCount();
  startPage = static_cast<int>(static_cast<int64_t>(chunkIndex) * pageCount / chunkCount);
  endPage = static_cast<int>(static_cast<int64_t>(chunkIndex + 1) * pageCount / chunkCount);
}

std::string PdfReaderActivity::chunkCachePath(const int chunkIndex) const {
  return pdf->getCachePath() + "/chunks/" + std::to_string(chunkIndex) + ".bin";
}

bool PdfReaderActivity::chunkCacheIsValid(const int chunkIndex) const {
  FsFile file;
  if (!SdMan.openFileForRead("PDR", chunkCachePath(chunkIndex), file)) return false;
  const ChunkHeader header = readChunkHeader(file, cachedFontId, cachedHeaderFontId, viewportWidth, viewportHeight,
                                             cachedParagraphAlignment, cachedExtraParagraphSpacing,
                                             cachedLineCompression, cachedWordSpacingFactor);
  file.close();
  return header.valid;
}

bool PdfReaderActivity::loadChunkIntoActiveState(const int chunkIndex) {
  if (pagesFile) pagesFile.close();
  pageFileOffsets.clear();

  if (!SdMan.openFileForRead("PDR", chunkCachePath(chunkIndex), pagesFile)) return false;

  const ChunkHeader header = readChunkHeader(pagesFile, cachedFontId, cachedHeaderFontId, viewportWidth,
                                             viewportHeight, cachedParagraphAlignment, cachedExtraParagraphSpacing,
                                             cachedLineCompression, cachedWordSpacingFactor);
  if (!header.valid) {
    INX_SERIAL.printf("[%lu] [PDR] Chunk %d cache stale/invalid\n", millis(), chunkIndex);
    pagesFile.close();
    return false;
  }

  pageFileOffsets.resize(header.pageCount);
  for (uint32_t i = 0; i < header.pageCount; i++) {
    serialization::readPod(pagesFile, pageFileOffsets[i]);
  }
  totalPages = static_cast<int>(header.pageCount);
  INX_SERIAL.printf("[%lu] [PDR] Loaded chunk %d: %d pages\n", millis(), chunkIndex, totalPages);
  return true;
}

void PdfReaderActivity::buildChunkCache(const int chunkIndex, const ChunkProgressUi ui) {
  int startPage = 0, endPage = 0;
  chunkPageRange(chunkIndex, startPage, endPage);

  imagesCached = 0;
  overlayPopupShown = false;
  reportBuildProgress(ui, "Reading PDF...", 0, 0, /*force=*/true);

  INX_SERIAL.printf("[%lu] [PDR] Extracting chunk %d (source pages %d-%d)...\n", millis(), chunkIndex, startPage,
                    endPage);
  const std::vector<PdfParagraph> paragraphs = pdf->extractStyledParagraphs(startPage, endPage);
  INX_SERIAL.printf("[%lu] [PDR] Extracted %zu paragraphs, laying out chunk %d...\n", millis(), paragraphs.size(),
                    chunkIndex);
  reportBuildProgress(ui, "Laying out pages...", 0, static_cast<int>(paragraphs.size()), /*force=*/true);

  const int bodyLineHeight = std::max(1, static_cast<int>(renderer.text.getLineHeight(cachedFontId) * cachedLineCompression));
  const int headerLineHeight =
      std::max(1, static_cast<int>(renderer.text.getLineHeight(cachedHeaderFontId) * cachedLineCompression));
  const TextBlock::Style alignment = toTextBlockAlignment(cachedParagraphAlignment);
  const int bodySpaceWidth =
      std::max(1, static_cast<int>(renderer.text.getWidth(cachedFontId, " ") * cachedWordSpacingFactor));
  const int headerSpaceWidth =
      std::max(1, static_cast<int>(renderer.text.getWidth(cachedHeaderFontId, " ") * cachedWordSpacingFactor));
  const std::string imageDir = pdf->getCachePath() + "/images";

  SdMan.mkdir((pdf->getCachePath() + "/chunks").c_str());
  const std::string scratchPath = pdf->getCachePath() + "/pages.tmp";
  FsFile scratch;
  if (!SdMan.openFileForWrite("PDR", scratchPath, scratch)) return;
  std::vector<uint32_t> offsets;

  auto page = std::make_unique<Page>();
  int yCursor = 0;

  auto flushPage = [&]() {
    if (!page->elements.empty()) {
      offsets.push_back(static_cast<uint32_t>(scratch.position()));
      page->serialize(scratch);
      maybeYield();
    }
    page = std::make_unique<Page>();
    yCursor = 0;
  };
  auto ensureRoom = [&](const int neededHeight) {
    if (yCursor > 0 && yCursor + neededHeight > viewportHeight) flushPage();
  };

  auto commitLine = [&](std::vector<std::string>& words, std::vector<int16_t>& xpos,
                        std::vector<EpdFontFamily::Style>& styles, const bool heading, const int lineIndent,
                        const int naturalEndX, const int lineSpaceWidth, const bool isLastLine) {
    if (words.empty()) return;
    const int lineHeight = heading ? headerLineHeight : bodyLineHeight;
    ensureRoom(lineHeight);

    const int gapCount = static_cast<int>(words.size()) - 1;
    if (alignment == TextBlock::JUSTIFIED && !heading && !isLastLine && gapCount > 0) {
      const int usedWidth = naturalEndX - lineSpaceWidth - lineIndent;
      const int spareSpace = (viewportWidth - lineIndent) - usedWidth;
      const int extraPerGap = std::max(-(lineSpaceWidth - 1), spareSpace / gapCount);
      const int remainder = spareSpace - extraPerGap * gapCount;
      int shift = 0;
      for (int i = 1; i < static_cast<int>(xpos.size()); i++) {
        shift += extraPerGap + (i - 1 < remainder ? 1 : 0);
        xpos[static_cast<size_t>(i)] =
            static_cast<int16_t>(std::clamp(xpos[static_cast<size_t>(i)] + shift, 0, viewportWidth));
      }
    }

    TextBlock block(words, xpos, styles, std::vector<uint8_t>(words.size(), 0), std::vector<uint8_t>(words.size(), 0),
                    alignment);
    if (heading) {
      page->elements.push_back(std::make_unique<PageHeader>(std::move(block), static_cast<int16_t>(lineIndent),
                                                            static_cast<int16_t>(yCursor), cachedHeaderFontId));
    } else {
      page->elements.push_back(
          std::make_unique<PageLine>(std::move(block), static_cast<int16_t>(lineIndent), static_cast<int16_t>(yCursor)));
    }
    yCursor += lineHeight;
    words.clear();
    xpos.clear();
    styles.clear();
  };

  for (size_t paragraphIndex = 0; paragraphIndex < paragraphs.size(); paragraphIndex++) {
    const PdfParagraph& paragraph = paragraphs[paragraphIndex];
    reportBuildProgress(ui, "Laying out pages...", static_cast<int>(paragraphIndex), static_cast<int>(paragraphs.size()));
    if (paragraph.isImage()) {
      if (paragraph.imageIntrinsicWidth <= 0 || paragraph.imageIntrinsicHeight <= 0) continue;

      int dispW = viewportWidth;
      int dispH = static_cast<int>(static_cast<int64_t>(paragraph.imageIntrinsicHeight) * dispW /
                                   paragraph.imageIntrinsicWidth);
      if (dispH < 1) dispH = 1;
      if (dispH > viewportHeight * 3) {
        dispH = viewportHeight * 3;
        dispW = static_cast<int>(static_cast<int64_t>(paragraph.imageIntrinsicWidth) * dispH /
                                 paragraph.imageIntrinsicHeight);
      }
      ensureRoom(std::min(dispH, viewportHeight));

      SdMan.mkdir(imageDir.c_str());
      const std::string imagePath =
          imageDir + "/" + std::to_string(chunkIndex) + "_" + std::to_string(imagesCached++) + ".jpg";
      FsFile imageFile;
      if (SdMan.openFileForWrite("PDR", imagePath, imageFile)) {
        imageFile.write(paragraph.imageJpegData.data(), paragraph.imageJpegData.size());
        imageFile.close();
        page->elements.push_back(std::make_unique<PageImage>(imagePath, imagePath, static_cast<int16_t>(dispW),
                                                              static_cast<int16_t>(dispH), 0,
                                                              static_cast<int16_t>(yCursor)));
        yCursor += dispH;
      }
      maybeYield();
      continue;
    }

    if (paragraph.words.empty()) continue;

    if (paragraph.heading && yCursor > 0) flushPage();

    std::vector<std::string> lineWords;
    std::vector<int16_t> lineXpos;
    std::vector<EpdFontFamily::Style> lineStyles;
    const int fontIdForWidth = paragraph.heading ? cachedHeaderFontId : cachedFontId;
    const int spaceWidth = paragraph.heading ? headerSpaceWidth : bodySpaceWidth;
    int paragraphSourceLeft = 0;
    bool haveParagraphSourceLeft = false;
    for (const auto& word : paragraph.words) {
      if (!haveParagraphSourceLeft || word.sourceX < paragraphSourceLeft) {
        paragraphSourceLeft = word.sourceX;
        haveParagraphSourceLeft = true;
      }
    }
    const int maxIndent = std::max(0, viewportWidth / 3);
    auto sourceIndentPx = [&](const PdfStyledWord& word) {
      return std::clamp(static_cast<int>(word.sourceX) - paragraphSourceLeft, 0, maxIndent);
    };
    int x = 0;
    int lineIndent = 0;
    bool paragraphStart = true;

    for (const auto& word : paragraph.words) {
      const EpdFontFamily::Style style = paragraph.heading ? EpdFontFamily::BOLD : word.style;
      const int wordWidth = renderer.text.getWidth(fontIdForWidth, word.text.c_str(), style);
      const bool sourceLineStart = paragraphStart || word.lineBreakBefore;

      if (!lineWords.empty() && x + wordWidth > viewportWidth) {
        commitLine(lineWords, lineXpos, lineStyles, paragraph.heading, lineIndent, x, spaceWidth,
                  /*isLastLine=*/false);
      }
      if (lineWords.empty()) {
        lineIndent = sourceLineStart ? sourceIndentPx(word) : 0;
        x = lineIndent;
      }
      lineXpos.push_back(static_cast<int16_t>(std::clamp(x - lineIndent, 0, viewportWidth)));
      lineWords.push_back(word.text);
      lineStyles.push_back(style);
      x += wordWidth + spaceWidth;
      paragraphStart = false;
      maybeYield();
    }
    commitLine(lineWords, lineXpos, lineStyles, paragraph.heading, lineIndent, x, spaceWidth, /*isLastLine=*/true);

    if (cachedExtraParagraphSpacing) {
      yCursor += bodyLineHeight / 2;
      if (yCursor > viewportHeight) flushPage();
    }
  }
  flushPage();
  scratch.close();

  INX_SERIAL.printf("[%lu] [PDR] Laid out chunk %d: %zu pages, writing cache...\n", millis(), chunkIndex,
                    offsets.size());
  reportBuildProgress(ui, "Saving...", static_cast<int>(offsets.size()), static_cast<int>(offsets.size()),
                      /*force=*/true);

  const uint32_t headerSize = 4 + 4 + 4 + 4 + 4 + 4 + 1 + 1 + 4 + 4 + 4;
  const uint32_t lutSize = static_cast<uint32_t>(offsets.size()) * sizeof(uint32_t);
  const uint32_t dataStart = headerSize + lutSize;
  for (auto& offset : offsets) offset += dataStart;

  {
    FsFile out;
    if (!SdMan.openFileForWrite("PDR", chunkCachePath(chunkIndex), out)) {
      SdMan.remove(scratchPath.c_str());
      return;
    }
    serialization::writePod(out, kPagesCacheMagic);
    serialization::writePod(out, kPagesCacheVersion);
    serialization::writePod(out, static_cast<int32_t>(cachedFontId));
    serialization::writePod(out, static_cast<int32_t>(cachedHeaderFontId));
    serialization::writePod(out, static_cast<int32_t>(viewportWidth));
    serialization::writePod(out, static_cast<int32_t>(viewportHeight));
    serialization::writePod(out, cachedParagraphAlignment);
    serialization::writePod(out, cachedExtraParagraphSpacing);
    serialization::writePod(out, cachedLineCompression);
    serialization::writePod(out, cachedWordSpacingFactor);
    serialization::writePod(out, static_cast<uint32_t>(offsets.size()));
    for (const uint32_t offset : offsets) serialization::writePod(out, offset);

    FsFile scratchIn;
    if (SdMan.openFileForRead("PDR", scratchPath, scratchIn)) {
      uint8_t buf[1024];
      while (scratchIn.available()) {
        const size_t n = scratchIn.read(buf, sizeof(buf));
        out.write(buf, n);
        maybeYield();
      }
      scratchIn.close();
    }
    out.close();
  }
  SdMan.remove(scratchPath.c_str());

  INX_SERIAL.printf("[%lu] [PDR] Chunk %d cache written: %zu pages\n", millis(), chunkIndex, offsets.size());
}

void PdfReaderActivity::switchToChunk(const int chunkIndex, const int startOnPage, const ChunkProgressUi ui) {
  if (!chunkCacheIsValid(chunkIndex)) {
    buildChunkCache(chunkIndex, ui);
  }
  if (!loadChunkIntoActiveState(chunkIndex)) {
    INX_SERIAL.printf("[%lu] [PDR] Failed to load chunk %d after build\n", millis(), chunkIndex);
    totalPages = 0;
  }
  currentChunk = chunkIndex;
  currentPage = std::clamp(startOnPage, 0, std::max(0, totalPages - 1));
  nextChunkPrefetchAttempted = false;
}

void PdfReaderActivity::serviceIdleNextChunkBuild() {
  if (nextChunkPrefetchAttempted) return;
  const int nextChunk = currentChunk + 1;
  if (nextChunk >= chunkCount || chunkCacheIsValid(nextChunk)) {
    nextChunkPrefetchAttempted = true;
    return;
  }

  INX_SERIAL.printf("[%lu] [PDR] Idle - prefetching chunk %d...\n", millis(), nextChunk);
  backgroundBuildActive = true;
  try {
    buildChunkCache(nextChunk, ChunkProgressUi::None);
    nextChunkPrefetchAttempted = true;
    INX_SERIAL.printf("[%lu] [PDR] Prefetch of chunk %d complete\n", millis(), nextChunk);
  } catch (const BuildAborted&) {
    INX_SERIAL.printf("[%lu] [PDR] Prefetch of chunk %d aborted by user input, will retry next idle window\n",
                      millis(), nextChunk);
  } catch (const std::bad_alloc&) {
    INX_SERIAL.printf("[%lu] [PDR] Prefetch of chunk %d ran out of memory\n", millis(), nextChunk);
    nextChunkPrefetchAttempted = true;
  }
  backgroundBuildActive = false;
}

void PdfReaderActivity::loadCurrentPageData() {
  currentPageData.reset();
  if (!pagesFile || currentPage < 0 || currentPage >= totalPages) return;
  if (!pagesFile.seek(pageFileOffsets[static_cast<size_t>(currentPage)])) return;
  currentPageData = Page::deserialize(pagesFile);
}

void PdfReaderActivity::prepareBook() {
  renderBuildProgress("Opening book...", 0, 0, /*force=*/true);

  ensureThumbnailExists();
  computeLayoutMetrics();

  chunkCount = std::max(1, std::min(kChunkCount, pdf->getPageCount()));
  loadProgress();

  switchToChunk(currentChunk, currentPage, ChunkProgressUi::FullScreen);
}

void PdfReaderActivity::renderScreen() {
  if (!pdf) {
    return;
  }

  if (!textReady) {
    prepareBook();
    textReady = true;
  }

  if (currentPage < 0 && currentChunk > 0) {
    switchToChunk(currentChunk - 1, std::numeric_limits<int>::max(), ChunkProgressUi::Overlay);
  } else if (currentPage >= totalPages && currentChunk < chunkCount - 1) {
    switchToChunk(currentChunk + 1, 0, ChunkProgressUi::Overlay);
  }

  if (totalPages == 0) {
    renderer.clearScreen();
    renderer.text.centered(MONTSERRAT_12_FONT_ID, 300, "Empty book", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (currentPage < 0) currentPage = 0;
  if (currentPage >= totalPages) currentPage = totalPages - 1;

  loadCurrentPageData();

  renderer.clearScreen();
  renderPage();

  saveProgress();
}

void PdfReaderActivity::renderPage() {
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += cachedScreenMargin;
  orientedMarginLeft += cachedScreenMargin;

  auto renderPageContent = [&]() {
    if (currentPageData) {
      currentPageData->render(renderer, cachedFontId, cachedHeaderFontId, orientedMarginLeft, orientedMarginTop,
                              /*skipImages=*/false, ImageRenderMode::OneBit, /*skipOnlyGrayscaleImages=*/false);
    }
  };

  renderPageContent();

  renderer.displayBufferAsync();

  if (READER_SETTINGS.textAntiAliasing && renderer.text.supportsAntiAliasing(cachedFontId)) {
    renderer.storeBwBuffer();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderPageContent();
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderPageContent();
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer(false, /*trackForRevert=*/false);
    renderer.setRenderMode(GfxRenderer::BW);

    renderer.restoreBwBuffer();
  }
}

void PdfReaderActivity::ensureThumbnailExists() {
  if (!pdf) return;
  const std::string thumbPath = pdf->getThumbBmpPath();
  if (!SdMan.exists(thumbPath.c_str())) {
    pdf->generateThumbBmp(renderer);
  }
}

void PdfReaderActivity::saveProgress() const {
  FsFile f;
  if (SdMan.openFileForWrite("PDR", pdf->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    data[0] = currentChunk & 0xFF;
    data[1] = (currentChunk >> 8) & 0xFF;
    data[2] = currentPage & 0xFF;
    data[3] = (currentPage >> 8) & 0xFF;
    f.write(data, 4);
    f.close();
  }
}

void PdfReaderActivity::loadProgress() {
  FsFile f;
  if (SdMan.openFileForRead("PDR", pdf->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentChunk = std::clamp(static_cast<int>(data[0] | (data[1] << 8)), 0, chunkCount - 1);
      currentPage = std::max(0, static_cast<int>(data[2] | (data[3] << 8)));
    }
    f.close();
  }
}

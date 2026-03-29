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
// Own cache format, not epub's: just enough to validate the cache still matches the current reading
// font/viewport/alignment (a settings change invalidates and rebuilds it) and to locate each page.
constexpr uint32_t kPagesCacheMagic = 0x50444650u;  // "PFDP"
constexpr uint32_t kPagesCacheVersion = 5u;  // now one file per chunk (see chunkCachePath()), not one per book
// Content-stream interpretation and page layout involve a recursive object parser and several layers of
// std::string/std::vector/std::function locals - too deep for the small internal-SRAM stack of whatever task
// calls into it (this activity's own onEnter()/loop() run on the shared UI task), so that work happens on a
// dedicated task with its stack allocated from PSRAM instead.
constexpr uint32_t kPdfWorkerStackBytes = 16384;
// PDFs have no chapter/TOC structure to key incremental building off of like EPUB's spine, so the book is
// instead split into up to this many roughly-equal source-page-range chunks (see chunkPageRange()) - short
// books get fewer, down to one chunk per page for anything under kChunkCount pages.
constexpr int kChunkCount = 10;
// How long the reader must sit idle (no page turn/input) before the worker task opportunistically starts
// building the next chunk in the background - mirrors EpubActivity's own idle-section-prefetch debounce.
constexpr unsigned long kIdleNextChunkBuildDelayMs = 1500;

TextBlock::Style toTextBlockAlignment(const uint8_t paragraphAlignment) {
  switch (paragraphAlignment) {
    case SystemSetting::CENTER_ALIGN:
      return TextBlock::CENTER_ALIGN;
    case SystemSetting::RIGHT_ALIGN:
      return TextBlock::RIGHT_ALIGN;
    case SystemSetting::JUSTIFIED:
    case SystemSetting::FOLLOW_CSS:
      return TextBlock::LEFT_ALIGN;  // justification needs per-line stretch this layout pass doesn't compute
    default:
      return TextBlock::LEFT_ALIGN;
  }
}

// Shared header layout for a chunk's cache file (see chunkCachePath()) - just enough to tell whether it still
// matches the current reading settings (a change invalidates and rebuilds only that chunk) and how many
// device pages it holds.
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
}  // namespace

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

  // On the touch reader, an upward swipe from the reading view closes the book - same gesture EPUB's
  // reader uses (see EpubActivity::loop()).
  if (mappedInput.hasTouch() && mappedInput.wasTouchSwipeUpForRenderer(renderer)) {
    onGoBack();
    return;
  }

  // Tap left half of the screen = previous page, right half = next page - same zones as
  // ReaderButtonBindings::handleInput() uses for EPUB.
  bool tapPrevTriggered = false;
  bool tapNextTriggered = false;
  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f, tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      (tapNx < 0.5f ? tapPrevTriggered : tapNextTriggered) = true;
    }
  }

  const bool usePressForPageTurn = READER_SETTINGS.longPressChapterSkip == SystemSetting::LONG_PRESS_OFF;
  const MappedInputManager::MotionGesture motionGesture = mappedInput.readMotionGesture(
      static_cast<uint8_t>(renderer.getOrientation()), SETTINGS.shakePageTurn, SETTINGS.shakePageTurnSensitivity);
  const bool prevTriggered = tapPrevTriggered || motionGesture == MappedInputManager::MotionGesture::Previous ||
                             (usePressForPageTurn ? (mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                                                     mappedInput.wasPressed(MappedInputManager::Button::Left))
                                                  : (mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                                                     mappedInput.wasReleased(MappedInputManager::Button::Left)));
  const bool powerPageTurn = READER_SETTINGS.btnPowerShortAction == SystemSetting::BTN_ACTION_PAGE_NEXT &&
                             mappedInput.wasReleased(MappedInputManager::Button::Power);
  const bool nextTriggered =
      tapNextTriggered || motionGesture == MappedInputManager::MotionGesture::Next ||
      (usePressForPageTurn ? (mappedInput.wasPressed(MappedInputManager::Button::PageForward) || powerPageTurn ||
                              mappedInput.wasPressed(MappedInputManager::Button::Right))
                           : (mappedInput.wasReleased(MappedInputManager::Button::PageForward) || powerPageTurn ||
                              mappedInput.wasReleased(MappedInputManager::Button::Right)));

  if (!prevTriggered && !nextTriggered) {
    return;
  }

  // Crossing past page 0 or the last page just walks off the current chunk's range (-1 / totalPages) -
  // renderScreen() (on the worker task, where it's safe to block on a chunk build) reconciles that into an
  // actual chunk switch. Kept this simple here since loop() runs on the shared UI task and must never itself
  // call into the PDF parser/layout code - see the class comment on why that work needs its own task/stack.
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
      // Not gated by renderingMutex: a background prefetch (ChunkProgressUi::None) never touches the renderer
      // or the *active* chunk's state (pagesFile/currentPageData/totalPages) - it only writes a different
      // chunk's cache file to disk, see buildChunkCache(). Taking the mutex here would also be
      // counterproductive: it would block renderScreen() from running at all until the whole prefetch
      // finished, defeating the point of maybeYield()'s BuildAborted interruption below.
      serviceIdleNextChunkBuild();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void PdfReaderActivity::maybeYield() {
  // This loop (this whole task, in fact) is single-threaded - updateRequired being set here means the UI
  // task wants a page rendered *right now*, and the only way to get back to displayTaskLoop()'s outer while
  // loop to service that is to unwind out of whatever depth of buildChunkCache() this call is nested in.
  // Never fires for the foreground/on-demand build (backgroundBuildActive is only set around a prefetch) -
  // the user is already waiting on that one, aborting it would just make them wait twice.
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
  if (!force && now - lastProgressRedrawMs < 800) return;  // e-ink refreshes are slow - don't redraw every tick
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
    // Draws over whatever's already in the framebuffer - no clearScreen() - since the reader was already
    // looking at the previous chunk's last page, not a blank/loading screen.
    overlayPopupLayout = ScreenComponents::drawPopup(renderer, "Loading pages...");
    overlayPopupShown = true;
    lastProgressRedrawMs = now;
    return;
  }
  if (!force && now - lastProgressRedrawMs < 800) return;  // e-ink refreshes are slow - don't redraw every tick
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
  overlayPopupShown = false;  // fresh build - draw a new popup rather than reusing a stale layout
  reportBuildProgress(ui, "Reading PDF...", 0, 0, /*force=*/true);

  INX_SERIAL.printf("[%lu] [PDR] Extracting chunk %d (source pages %d-%d)...\n", millis(), chunkIndex, startPage,
                    endPage);
  const std::vector<PdfParagraph> paragraphs = pdf->extractStyledParagraphs(startPage, endPage);
  INX_SERIAL.printf("[%lu] [PDR] Extracted %zu paragraphs, laying out chunk %d...\n", millis(), paragraphs.size(),
                    chunkIndex);
  reportBuildProgress(ui, "Laying out pages...", 0, static_cast<int>(paragraphs.size()), /*force=*/true);

  // Both multipliers come from the user's actual reading preset (READER_SETTINGS "line height %" / "text
  // spacing %"), the same values EPUB's own Section layout applies - without them, PDF pages ignore the
  // reader's configured spacing entirely and just use each font's untouched natural metrics.
  const int bodyLineHeight = std::max(1, static_cast<int>(renderer.text.getLineHeight(cachedFontId) * cachedLineCompression));
  const int headerLineHeight =
      std::max(1, static_cast<int>(renderer.text.getLineHeight(cachedHeaderFontId) * cachedLineCompression));
  const TextBlock::Style alignment = toTextBlockAlignment(cachedParagraphAlignment);
  const int bodySpaceWidth =
      std::max(1, static_cast<int>(renderer.text.getWidth(cachedFontId, " ") * cachedWordSpacingFactor));
  const int headerSpaceWidth =
      std::max(1, static_cast<int>(renderer.text.getWidth(cachedHeaderFontId, " ") * cachedWordSpacingFactor));
  const std::string imageDir = pdf->getCachePath() + "/images";

  // Two-pass write (scratch file now, header+LUT written in front of it once the page count is known) rather
  // than seeking back to patch a placeholder LUT after the fact - matches the pattern already used elsewhere
  // in this codebase (see Mobi::convertToEpub) for a file whose header depends on content written after it.
  // Each page is serialized to the scratch file and destroyed the moment it's full, immediately freeing its
  // PSRAM - never holding more than one page's worth of Page/TextBlock objects in memory at once, the same
  // way EPUB's own Section builds a chapter (see onPageComplete in Section.cpp). Holding the *whole* book's
  // pages in memory at once (an earlier version of this code did exactly that) was enough to exhaust PSRAM
  // and crash with an uncaught std::bad_alloc on a real book. Builds are always sequential on this activity's
  // one worker task (never the foreground and a background prefetch at once), so a single shared scratch
  // filename is safe.
  SdMan.mkdir((pdf->getCachePath() + "/chunks").c_str());
  const std::string scratchPath = pdf->getCachePath() + "/pages.tmp";
  FsFile scratch;
  if (!SdMan.openFileForWrite("PDR", scratchPath, scratch)) return;
  std::vector<uint32_t> offsets;

  auto page = std::make_unique<Page>();
  int yCursor = 0;  // pixels used so far on the current page

  auto flushPage = [&]() {
    if (!page->elements.empty()) {
      offsets.push_back(static_cast<uint32_t>(scratch.position()));
      page->serialize(scratch);
      maybeYield();
    }
    page = std::make_unique<Page>();  // destroys the old page here, freeing its PSRAM immediately
    yCursor = 0;
  };
  // Starts a fresh page first if this element wouldn't fit the remaining space - unless the page is still
  // empty, in which case there's nowhere better to put it (an oversized single line/image just overflows).
  auto ensureRoom = [&](const int neededHeight) {
    if (yCursor > 0 && yCursor + neededHeight > viewportHeight) flushPage();
  };

  auto commitLine = [&](std::vector<std::string>& words, std::vector<int16_t>& xpos,
                        std::vector<EpdFontFamily::Style>& styles, const bool heading, const int lineIndent,
                        const int naturalEndX, const int lineSpaceWidth, const bool isLastLine) {
    if (words.empty()) return;
    const int lineHeight = heading ? headerLineHeight : bodyLineHeight;
    ensureRoom(lineHeight);

    // Stretch inter-word gaps to fill the line under justified alignment, matching EPUB's own
    // ParsedText::extractLine() algorithm. Skipped on a paragraph's last line (a short final line stays
    // left-aligned rather than stretched to the margin - standard typographic practice) and for headings
    // (short/centered by nature; stretching would look wrong).
    const int gapCount = static_cast<int>(words.size()) - 1;
    if (alignment == TextBlock::JUSTIFIED && !heading && !isLastLine && gapCount > 0) {
      const int usedWidth = naturalEndX - lineSpaceWidth - lineIndent;
      const int spareSpace = (viewportWidth - lineIndent) - usedWidth;
      // Never let a gap collapse to zero/negative (overlapping words) even if this line was, edge-case,
      // computed very slightly overfull - tighten toward 1px instead of crossing it.
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
      if (dispH > viewportHeight * 3) {  // don't let one image span an unreasonable number of pages
        dispH = viewportHeight * 3;
        dispW = static_cast<int>(static_cast<int64_t>(paragraph.imageIntrinsicWidth) * dispH /
                                 paragraph.imageIntrinsicHeight);
      }
      ensureRoom(std::min(dispH, viewportHeight));

      SdMan.mkdir(imageDir.c_str());
      // Chunk-prefixed so two independently-built chunks never reuse the same filename - each chunk's own
      // imagesCached counter restarts at 0.
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

    // Give headings/section starts a fresh page instead of letting them land squeezed under whatever
    // preceding content happened to end mid-page - the fill-until-full body text below has no other sense
    // of "section boundary" to break on, so without this every page break looks arbitrary.
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
    // PDF coordinates are in points while this is a reflow layout in device pixels. A conservative 1:1
    // conversion preserves the relationship between a bullet, its text, and wrapped continuation lines
    // without importing the whole page's original margins into the reader.
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
      // Only the paragraph's first word and words the extractor flagged as starting a new source line carry
      // a meaningful left position - anywhere else sourceX is just the enclosing run's start x, not real
      // indentation. Crucially, a source line boundary does NOT force a device line break here: PDF body
      // text is normally wrapped to the *source* page's width, which is usually much wider than this
      // device's viewport, so forcing a break at every one of those boundaries would end most device lines
      // early and throw away most of their remaining width instead of reflowing. Width-driven wraps (below)
      // decide breaks; source line starts only ever affect indentation.
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

    // Blank gap between paragraphs, matching how every other reader in this app visually separates them -
    // only when the user's "extra paragraph spacing" setting is actually on.
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

  // magic+version+fontId+headerFontId+vw+vh+align+extraParaSpacing+lineCompression+wordSpacingFactor+pageCount
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

  // Deliberately doesn't touch totalPages/pagesFile/currentPage: this may have just been a background
  // prefetch of a chunk the reader isn't showing yet - only switchToChunk()/loadChunkIntoActiveState() are
  // allowed to change what's actually on screen.
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
  nextChunkPrefetchAttempted = false;  // new active chunk - reconsider prefetching its successor
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
    nextChunkPrefetchAttempted = true;  // don't retry a build that's known to OOM every idle window
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
  // Painted before anything else so there's no blank/stale-screen gap while ensureThumbnailExists() (which
  // can render a full cover page) and layout metrics run - both happen before switchToChunk() would
  // otherwise show this same screen.
  renderBuildProgress("Opening book...", 0, 0, /*force=*/true);

  ensureThumbnailExists();
  computeLayoutMetrics();

  chunkCount = std::max(1, std::min(kChunkCount, pdf->getPageCount()));
  loadProgress();  // sets currentChunk/currentPage to the saved position, or 0/0 if there is none

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

  // loop() lets currentPage walk one step past either end of the current chunk (-1 / totalPages) as the
  // signal that a page turn crossed a chunk boundary - reconcile that here, on the worker task, since
  // switching chunks may mean blocking on a fresh chunk build (see switchToChunk()).
  // A page turn while already reading (as opposed to the book's very first chunk in prepareBook()) shows a
  // small overlay popup instead of the full-screen view - the reader is looking at real content, not a
  // blank loading screen, so there's no need to hide it.
  if (currentPage < 0 && currentChunk > 0) {
    // Lands on the new chunk's last page.
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

  // Async: any AA grayscale pass below already blocks on the refresh finishing before it touches the
  // panel again (every StickyDisplay call starts with finish()), so this only stops blocking a plain
  // turn (no AA pass) on the ~600ms e-ink refresh - default (fast) refresh on every turn, no periodic
  // forced full/half flash.
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
      // Clamped only against chunkCount here - the saved chunk's actual page count isn't known until
      // switchToChunk() below loads (or builds) it, which does its own final clamp against that.
      currentChunk = std::clamp(static_cast<int>(data[0] | (data[1] << 8)), 0, chunkCount - 1);
      currentPage = std::max(0, static_cast<int>(data[2] | (data[3] << 8)));
    }
    f.close();
  }
}

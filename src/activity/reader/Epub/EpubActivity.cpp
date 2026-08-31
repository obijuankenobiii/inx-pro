/**
 * @file EpubActivity.cpp
 * @brief Definitions for EpubActivity.
 */

#include "EpubActivity.h"

#include <Bitmap.h>
#include <Epub/Page.h>
#include <Epub/PageWordIndex.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <ImageRender.h>
#include <SDCardManager.h>
#include <esp_task_wdt.h>
#include <time.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "EpubAnnotations.h"
#include "EpubNavigation.h"
#include "EpubReadingStats.h"
#include "ReadingGuideLines.h"
#if FREEINK_CAP_MIC
#include "VoiceNoteActivity.h"
#endif
#include "KOReaderSyncActivity.h"
#include "SettingsDrawer.h"
#include "activity/page/components/global/PopUp.h"
#include "activity/util/KeyboardEntryActivity.h"
#include "state/BookProgress.h"
#include "state/BookSetting.h"
#include "state/BookState.h"
#include "state/RecentBooks.h"
#include "state/ReaderPreset.h"
#include "state/Session.h"
#include "state/Statistics.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/Frontlight.h"
#include "system/FrontlightPreferences.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"

namespace {
constexpr unsigned long goHomeMs = 1000;
constexpr unsigned long bookmarkHoldMs = 1000;
constexpr unsigned long wordSelectionHoldMs = 500;
constexpr bool kReaderHighQualityFastLut = true;

// Base word-action list; EpubActivity::currentWordActions() appends "View footnote" conditionally.
const std::vector<std::string> kBaseWordActions = {"Look up", "Highlight", "Add note"};

bool pageImageFootprintAtLeastHalfScreen(const Page& page, const GfxRenderer& renderer, int marginLeft, int marginTop) {
  if (!page.hasImages()) {
    return false;
  }
  int16_t ix = 0;
  int16_t iy = 0;
  int16_t iw = 0;
  int16_t ih = 0;
  if (!page.getImageBoundingBox(renderer, marginLeft, marginTop, ix, iy, iw, ih)) {
    return false;
  }
  const int halfW = renderer.getScreenWidth() / 2;
  const int halfH = renderer.getScreenHeight() / 2;
  return iw >= halfW && ih >= halfH;
}

uint32_t addPreparedPageSignature(uint32_t hash, const uint32_t value) {
  hash ^= value;
  return hash * 16777619u;
}

uint32_t floatBits(const float value) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "float signature size");
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}
}  // namespace

/**
 * @brief Constructs a new EpubActivity
 * @param renderer Reference to the graphics renderer
 * @param mappedInput Reference to the input manager
 * @param epub Unique pointer to the EPUB document
 * @param onGoBack Callback for returning to previous activity
 * @param onGoToHome Callback for navigating to Home
 */
EpubActivity::EpubActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub,
                           const std::function<void()>& onGoBack, const std::function<void()>& onGoToHome,
                           const int initialSpineIndex, const int initialPageNumber)
    : ActivityWithSubactivity("EpubReader", renderer, mappedInput),
      currentFontId(0),
      nextFontId(0),
      epub(std::move(epub)),
      onGoBack(onGoBack),
      onGoToHome(onGoToHome),
      currentSpineIndex(0),
      nextPageNumber(0),
      initialSpineIndex_(initialSpineIndex),
      initialPageNumber_(initialPageNumber),
      pagesUntilFullRefresh(0),
      cachedSpineIndex(0),
      cachedChapterTotalPageCount(0),
      updateRequired(false),
      loadingProgress(0),
      showBookmarkIndicator(false),
      lastPageHadImages(false),
      lastPageHadLargeImage(false),
      lastPageHadHighQualityImage(false),
      settingsDrawer(nullptr),
      settingsDrawerVisible(false),
      bookProgress(nullptr) {
  loadBookSettings();
  navigation_ = std::make_unique<EpubNavigation>(*this);
}

/**
 * @brief Calculates the viewport dimensions based on current settings
 * @return ViewportInfo structure containing viewport dimensions and settings
 */
ViewportInfo EpubActivity::calculateViewport() {
  ViewportInfo info;

  int oT, oR, oB, oL;
  renderer.getOrientedViewableTRBL(&oT, &oR, &oB, &oL);

  info.totalMarginTop = oT + bookSettings.screenMargin;
  info.totalMarginBottom = oB + StatusBar::reservedBottomMargin(renderer, bookSettings);
  info.totalMarginLeft = oL + bookSettings.screenMargin;
  info.totalMarginRight = oR + bookSettings.screenMargin;

  info.fontId = bookSettings.getReaderFontId();
  // Each line's baseline is (top + ascender), so the first line's cap top sits (ascender - capHeight)
  // below the margin. Remove that font leading so text starts at the screen margin (it otherwise looks
  // like a doubled top margin). Usable height grows by the same amount, so the bottom is unchanged.
  const int topInset = renderer.text.getGlyphTopInset(info.fontId, 'H', EpdFontFamily::REGULAR);
  info.totalMarginTop = std::max(oT, info.totalMarginTop - topInset);

  int w = renderer.getScreenWidth() - info.totalMarginLeft - info.totalMarginRight;
  int h = renderer.getScreenHeight() - info.totalMarginTop - info.totalMarginBottom;
  constexpr int kMinViewport = 8;
  if (w < kMinViewport) w = kMinViewport;
  if (h < kMinViewport) h = kMinViewport;
  info.width = static_cast<uint16_t>(w);
  info.height = static_cast<uint16_t>(h);

  info.lineCompression = bookSettings.getReaderLineCompression();
  info.wordSpacing = bookSettings.getReaderWordSpacingFactor();

  return info;
}

void EpubActivity::drawLoadingScreen() {
  invalidatePreparedPage();
  // displayBuffer() swaps Sticky's dual framebuffers. Copy the displayed frame back before
  // painting only the loading bar, otherwise the bar is drawn over stale library/white data.
  renderer.syncWriteBufferFromActive();
  const int barWidth = renderer.getScreenWidth();
  const int barHeight = 8;
  const int barX = 0;
  const int barY = renderer.getScreenHeight() - barHeight;

  renderer.rectangle.fill(barX, barY, barWidth, barHeight, false);
  renderer.rectangle.render(barX, barY, barWidth, barHeight, true);

  if (loadingProgress > 0) {
    int fillWidth = barWidth * loadingProgress / 100;
    if (fillWidth > 0) {
      fillWidth = std::max(1, fillWidth);
      renderer.rectangle.fill(barX + 1, barY + 1, std::max(1, fillWidth - 2), barHeight - 2, true);
    }
  }

  renderer.displayBuffer();
}

void EpubActivity::drawPreparingBookScreen() {
  invalidatePreparedPage();
  // Keep the preparation state as a normal full-screen frame.  In particular, do not use the
  // popup renderer here: it paints a temporary overlay and the following buffer swap can expose
  // the old library frame while the book is still being opened.
  renderer.clearScreen(0xff);
  renderer.text.centered(MONTSERRAT_12_FONT_ID, renderer.getScreenHeight() / 2,
                         "Preparing book...", true);
  renderer.displayBuffer();
}

void EpubActivity::readerPopup(const char* message) {
  invalidatePreparedPage();
  pauseReadingStats();
  renderer.syncWriteBufferFromActive();
  ScreenComponents::drawPopup(renderer, message);
}

void EpubActivity::handleChapterLoadFailure() {
  readerPopup("Error loading chapter");

  if (!chapterRecoveryAttempted_) {
    chapterRecoveryAttempted_ = true;
    currentSpineIndex = lastGoodSpineIndex_;
    nextPageNumber = lastGoodPageNumber_;
    section.reset();
    updateRequired = true;
    return;
  }

  chapterRecoveryAttempted_ = false;
  section.reset();
  if (epub) {
    epub->clearCache();
  }
  if (bookProgress) {
    bookProgress->remove();
  }
  onGoBack();
}

ScreenComponents::LoadingProgressLayout EpubActivity::loadingProgressShow(const char* message,
                                                                          const int progressPercent0to100) {
  renderer.syncWriteBufferFromActive();
  return ScreenComponents::LoadingProgress::show(renderer, message, progressPercent0to100);
}

/**
 * @brief Builds a section file for a given spine index
 * @param spineIndex Index of the spine to build
 * @param info Viewport information for rendering
 * @param showProgress Whether to show progress during building
 * @param skipImages If true, skip processing new images
 * @return true if successful, false otherwise
 */
bool EpubActivity::buildSection(int spineIndex, const ViewportInfo& info, bool showProgress, bool skipImages) {
  // Foreground generation must own the EPUB ZIP handles. An in-progress idle
  // build is disposable; its final file is never published until complete.
  if (!epub) return false;
  const int totalSpines = epub->getSpineItemsCount();
  if (spineIndex < 0 || spineIndex >= totalSpines) {
    INX_SERIAL.printf("[%lu] [EPA] buildSection: invalid spine=%d total=%d\n", millis(), spineIndex, totalSpines);
    return false;
  }
  const std::string cachePath = epub->getCachePath();
  // Section files live under sections/*.bin (see Section ctor). Legacy .sec at cache root was never used here.
  const std::string sectionBinPath = cachePath + "/sections/" + std::to_string(spineIndex) + ".bin";
  const std::string legacySecPath = cachePath + "/" + std::to_string(spineIndex) + ".sec";
  if (SdMan.exists(legacySecPath.c_str())) {
    SdMan.remove(legacySecPath.c_str());
  }
  if (SdMan.exists(sectionBinPath.c_str())) {
    SdMan.remove(sectionBinPath.c_str());
  }

  std::shared_ptr<Epub> sharedEpub = std::shared_ptr<Epub>(epub.get(), [](Epub*) {});
  auto tempSection = std::unique_ptr<Section>(new Section(sharedEpub, spineIndex, renderer));

  ScreenComponents::PopupLayout chapterLoadPopup{};
  const bool useChapterLoadBar = showProgress;
  if (useChapterLoadBar) {
    renderer.syncWriteBufferFromActive();
    chapterLoadPopup = ScreenComponents::drawPopup(renderer, "Loading chapter...");
    ScreenComponents::fillPopupProgress(renderer, chapterLoadPopup, 12);
  }

  bool success = tempSection->createSectionFile(
      info.fontId, FontManager::getNextFont(info.fontId), FontManager::getMaxFontId(info.fontId), info.lineCompression,
      info.wordSpacing, bookSettings.extraParagraphSpacing, bookSettings.paragraphAlignment, info.width, info.height,
      bookSettings.hyphenationEnabled, bookSettings.paragraphCssIndentEnabled != 0,
      bookSettings.bionicReadingEnabled != 0, nullptr, skipImages, nullptr,
      // Image display caches are generated lazily by the visible-page render. Warming every image while
      // parsing made chapter opening pay for all decoder work and SD writes before the first page existed.
      /*warmImageDisplayCache=*/false,
      /*warmImageRenderMode=*/READER_SETTINGS.readerImageGrayscale != 0 ? ImageRenderMode::TwoBit
                                                                           : ImageRenderMode::OneBit,
      /*warmImageQuality=*/READER_SETTINGS.readerImageGrayscale == SystemSetting::READER_IMAGE_HIGH,
      info.totalMarginTop);

  if (success) {
    // This spine's pagination just changed (first build, or a font/size/margin change forced a rebuild) -
    // re-locate any existing highlights for it by their stored phrase before anything renders, so a
    // highlight created under one layout stays on the right words after switching to another. Cheap no-op
    // for the common case of a spine with no annotations.
    EpubAnnotations::migrateSpineAnnotations(cachePath, spineIndex, tempSection->pageCount, renderer, info.fontId,
                                             FontManager::getNextFont(info.fontId), info.totalMarginLeft,
                                             info.totalMarginTop);
    // Migration rewrites this spine's annotation shards. The in-memory page cache can still describe
    // the old pagination (including the same numeric page before and after a font change), so force the
    // next render to load the relocated highlight from disk and rebuild its word geometry.
    annUi_.annotations().clearSession();
    annUi_.storedRanges().clear();
    annUi_.clearWordIndexCache();
  }

  if (useChapterLoadBar) {
    ScreenComponents::fillPopupProgress(renderer, chapterLoadPopup, 100);
    renderer.clearScreen();
    renderer.displayBuffer();
  }

  return success;
}

/**
 * @brief Loads a section for a given spine index
 * @param spineIndex Index of the spine to load
 * @param info Viewport information for rendering
 * @return Unique pointer to the loaded section
 */
std::unique_ptr<Section> EpubActivity::loadSection(int spineIndex, const ViewportInfo& info, const bool showProgress) {
  if (!epub) return nullptr;
  const int totalSpines = epub->getSpineItemsCount();
  if (spineIndex < 0 || spineIndex >= totalSpines) {
    INX_SERIAL.printf("[%lu] [EPA] loadSection: invalid spine=%d total=%d\n", millis(), spineIndex, totalSpines);
    return nullptr;
  }

  std::shared_ptr<Epub> sharedEpub = std::shared_ptr<Epub>(epub.get(), [](Epub*) {});
  auto loadedSection = std::unique_ptr<Section>(new Section(sharedEpub, spineIndex, renderer));

  bool isCached = loadedSection->loadSectionFile(
      info.fontId, info.lineCompression, info.wordSpacing, bookSettings.extraParagraphSpacing,
      bookSettings.paragraphAlignment, info.width, info.height, bookSettings.hyphenationEnabled,
      bookSettings.paragraphCssIndentEnabled != 0, bookSettings.bionicReadingEnabled != 0);

  if (!isCached) {
    if (!buildSection(spineIndex, info, showProgress, false)) {
      INX_SERIAL.printf("[%lu] [EPA] loadSection: build failed spine=%d total=%d\n", millis(), spineIndex, totalSpines);
      return nullptr;
    }
    if (!loadedSection->loadSectionFile(
            info.fontId, info.lineCompression, info.wordSpacing, bookSettings.extraParagraphSpacing,
            bookSettings.paragraphAlignment, info.width, info.height, bookSettings.hyphenationEnabled,
            bookSettings.paragraphCssIndentEnabled != 0, bookSettings.bionicReadingEnabled != 0)) {
      INX_SERIAL.printf("[%lu] [EPA] loadSection: load after build failed spine=%d total=%d\n", millis(), spineIndex,
                    totalSpines);
      return nullptr;
    }
  }

  if (loadedSection->pageCount == 0) {
    INX_SERIAL.printf("[%lu] [EPA] loadSection: zero page section spine=%d total=%d\n", millis(), spineIndex, totalSpines);
    return nullptr;
  }

  return loadedSection;
}

/**
 * @brief Sets up orientation based on book settings
 */
void EpubActivity::setupOrientation() {
  switch (bookSettings.orientation) {
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
}

bool EpubActivity::syncSettingsFromGlobalIfNeeded() {
  if (bookSettings.useCustomSettings) {
    return false;
  }

  const BookSettings before = bookSettings;
  bookSettings.loadFromGlobalSettings();
  bookSettings.useCustomSettings = false;
  return bookSettings != before;
}

void EpubActivity::onBookSettingsLiveLayoutSync() {
  if (settingsDrawer) {
    settingsDrawer->relayoutForRendererChange();
  }
}

/**
 * @brief Loads progress from file using BookProgress handler
 */
void EpubActivity::loadProgress() {
  if (!bookProgress) {
    return;
  }

  BookProgress::Data data;
  int totalSpines = epub->getSpineItemsCount();

  if (bookProgress->load(data) && bookProgress->validate(data, totalSpines)) {
    currentSpineIndex = data.spineIndex;
    nextPageNumber = data.pageNumber;
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = data.chapterPageCount;
  } else {
    bookProgress->remove();
    currentSpineIndex = 0;
    nextPageNumber = 0;
    cachedSpineIndex = 0;
    cachedChapterTotalPageCount = 0;
  }
}

/**
 * @brief Saves current progress using BookProgress handler
 * @param spineIndex Current spine index
 * @param currentPage Current page number
 * @param pageCount Total pages in current chapter
 */
void EpubActivity::saveProgress(int spineIndex, int currentPage, int pageCount, const bool saveRecentNow) {
  if (!bookProgress || !epub) {
    return;
  }

  BookProgress::Data data;
  data.spineIndex = spineIndex;
  data.pageNumber = currentPage;
  data.chapterPageCount = pageCount;
  data.lastReadTimestamp = millis();

  if (pageCount > 0) {
    float spineProgress = static_cast<float>(currentPage) / static_cast<float>(pageCount);
    data.progressPercent = epub->calculateProgress(spineIndex, spineProgress) * 100.0f;
  }

  bookProgress->save(data);

  if (pageCount > 0) {
    float spineProgress = static_cast<float>(currentPage) / static_cast<float>(pageCount);
    float bookProgressValue = epub->calculateProgress(spineIndex, spineProgress);
    RECENT_BOOKS.addBook(epub->getPath(), epub->getCachePath(), epub->getTitle(), epub->getAuthor(), bookProgressValue,
                         saveRecentNow);
  }
}

/**
 * @brief Ensures thumbnail exists, generates if needed
 */
void EpubActivity::ensureThumbnailExists(const bool coverAvailable) {
  (void)coverAvailable;
  const bool generated = epub->generateThumbBmp(false);
  INX_SERIAL.printf("[%lu] [THUMB-GEN] result=%d jpg=%d png=%d bmp=%d cache=%s\n", millis(), generated ? 1 : 0,
                 SdMan.exists(epub->getThumbJpegPath().c_str()) ? 1 : 0,
                 SdMan.exists(epub->getThumbPngPath().c_str()) ? 1 : 0,
                 SdMan.exists(epub->getThumbBmpPath().c_str()) ? 1 : 0, epub->getCachePath().c_str());
}

/**
 * @brief Displays cover if it exists, otherwise shows title
 */
bool EpubActivity::displayCoverOrTitle() {
  const std::string coverJpegPath = epub->getCoverJpegPath(false);
  std::string coverPath = epub->getCoverBmpPath(false);
  if (!SdMan.exists(coverPath.c_str()) && !SdMan.exists(coverJpegPath.c_str())) {
    epub->generateCoverBmp(false);
  }

  if (SdMan.exists(coverJpegPath.c_str())) {
    const int pageWidth = renderer.getScreenWidth();
    const int pageHeight = renderer.getScreenHeight();
    renderer.clearScreen();
    ImageRender::Options options;
    options.cropToFill = true;
    // The slow path immediately moves on to thumbnail generation. Keep this
    // rendered cover plane in the PSRAM display cache, but defer its durable
    // SD write until the reader returns to the idle main loop.
    options.asyncDisplayCache = true;
    if (ImageRender::create(renderer, coverJpegPath).render(0, 0, pageWidth, pageHeight, options)) {
      renderer.displayBuffer();
      return true;
    }
  }

  if (SdMan.exists(coverPath.c_str())) {
    const int pageWidth = renderer.getScreenWidth();
    const int pageHeight = renderer.getScreenHeight();
    ImageRender::Options options;
    options.cropToFill = true;
    options.asyncDisplayCache = true;
    renderer.clearScreen();
    if (ImageRender::create(renderer, coverPath).render(0, 0, pageWidth, pageHeight, options)) {
      renderer.displayBuffer();
      return true;
    }
  } else {
    displayBookTitle();
  }
  return false;
}

/**
 * @brief Loads and sets up the current section
 */
void EpubActivity::loadCurrentSection(const bool showProgress) {
  if (!epub) {
      section.reset();
    return;
  }

  ViewportInfo info = calculateViewport();
  section.reset();
  const int totalSpines = epub->getSpineItemsCount();
  const int direction = nextPageNumber == static_cast<int>(UINT16_MAX) ? -1 : 1;
  const int requestedSpine = currentSpineIndex;
  int spineIndex = currentSpineIndex;

  while (spineIndex >= 0 && spineIndex < totalSpines) {
    auto newSection = loadSection(spineIndex, info, showProgress);
    if (!newSection) {
      INX_SERIAL.printf("[%lu] [EPA] loadCurrentSection: skipping non-renderable spine=%d direction=%d\n", millis(),
                    spineIndex, direction);
      spineIndex += direction;
      continue;
    }

    if (spineIndex != requestedSpine) {
      currentSpineIndex = spineIndex;
      nextPageNumber = direction < 0 ? static_cast<int>(UINT16_MAX) : 0;
      pendingPercentJump = false;
    }
    section = std::move(newSection);
    if (nextPageNumber == static_cast<int>(UINT16_MAX)) {
      section->currentPage = (section->pageCount > 0) ? (section->pageCount - 1) : 0;
    } else {
      section->currentPage = (nextPageNumber >= 0 && nextPageNumber < section->pageCount) ? nextPageNumber : 0;
    }

    if (cachedChapterTotalPageCount > 0 && currentSpineIndex == cachedSpineIndex &&
        section->pageCount != cachedChapterTotalPageCount) {
      float progress = static_cast<float>(section->currentPage) / static_cast<float>(cachedChapterTotalPageCount);
      int newPage = static_cast<int>(progress * section->pageCount);
      section->currentPage = std::min(newPage, section->pageCount - 1);
      cachedChapterTotalPageCount = 0;
    }
    return;
  }

  if (direction > 0) {
    currentSpineIndex = totalSpines;
    nextPageNumber = 0;
  }
}

/**
 * @brief Updates recent books and app state
 */
void EpubActivity::updateExternalState() {
  APP_STATE.lastRead = "";
  APP_STATE.saveToFile();

  float spineProgress = section ? static_cast<float>(section->currentPage) / section->pageCount : 0;
  float bookProgressValue = epub->calculateProgress(currentSpineIndex, spineProgress);
  RECENT_BOOKS.addBook(epub->getPath(), epub->getCachePath(), epub->getTitle(), epub->getAuthor(), bookProgressValue);
}

/**
 * @brief Fast path for books that were opened before
 */
void EpubActivity::fastPath() {
  loadProgress();
  FontManager::ensureReaderLayoutFonts(calculateViewport().fontId, renderer);
  int totalSpineItems = epub->getSpineItemsCount();
  if (currentSpineIndex >= totalSpineItems) {
    currentSpineIndex = 0;
    nextPageNumber = 0;
    cachedSpineIndex = 0;
    cachedChapterTotalPageCount = 0;
  }
  if (initialSpineIndex_ >= 0 && initialSpineIndex_ < totalSpineItems && initialPageNumber_ >= 0) {
    currentSpineIndex = initialSpineIndex_;
    nextPageNumber = initialPageNumber_;
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = 0;
  }

  loadCurrentSection();
  statusBar = std::unique_ptr<StatusBar>(new StatusBar(renderer, *epub, bookSettings, &readingStats_));
}

/**
 * @brief Slow path for new books
 */
bool EpubActivity::slowPath() {
  INX_SERIAL.printf("[%lu] [THUMB-GEN] slowPath start book=%s cache=%s\n", millis(), epub->getPath().c_str(),
                 epub->getCachePath().c_str());
  if (!epub->isLoaded() && !epub->load(true)) {
    readerPopup("Book seems corrupted");
    onGoBack();
    return false;
  }

  const bool coverAvailable = displayCoverOrTitle();
  loadingProgress = 30;
  drawLoadingScreen();
  vTaskDelay(pdMS_TO_TICKS(50));

  INX_SERIAL.printf("[%lu] [THUMB-GEN] slowPath calling thumbnail coverAvailable=%d\n", millis(),
                 coverAvailable ? 1 : 0);
  ensureThumbnailExists(coverAvailable);
  const int initialSpine = epub->getSpineIndexForInitialOpen();
  currentSpineIndex = (initialSpine == 0 && epub->getSpineItemsCount() > 1) ? 1 : initialSpine;
  nextPageNumber = 0;
  if (initialSpineIndex_ >= 0 && initialSpineIndex_ < epub->getSpineItemsCount() && initialPageNumber_ >= 0) {
    currentSpineIndex = initialSpineIndex_;
    nextPageNumber = initialPageNumber_;
  }

  FontManager::ensureReaderLayoutFonts(calculateViewport().fontId, renderer);
  BOOK_STATE.addOrUpdateBook(epub->getPath(), epub->getTitle(), epub->getAuthor());

  loadCurrentSection(false);
  loadingProgress = 100;
  drawLoadingScreen();

  statusBar = std::unique_ptr<StatusBar>(new StatusBar(renderer, *epub, bookSettings, &readingStats_));
  if (!section) {
    readerPopup("Error loading chapter");
    onGoBack();
    return false;
  }
  return true;
}

/**
 * @brief Called when entering the activity
 */
void EpubActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  epub->setupCacheDir();

  syncSettingsFromGlobalIfNeeded();
  setupOrientation();

  bookProgress.reset(new BookProgress(epub->getCachePath()));

  BookState::Book bookState;
  const bool isTracked = BOOK_STATE.findBook(epub->getPath(), bookState);
  bool hasProgress = bookProgress->exists();
  const bool useFastPath = (epub->isLoaded() || epub->hasMetadataCache()) && isTracked && hasProgress;

  if (!useFastPath) {
    drawPreparingBookScreen();
  }

  if (useFastPath) {
    fastPath();
  } else {
    if (!slowPath()) {
      return;
    }
  }

  updateExternalState();
  BOOK_STATE.setReading(epub->getPath(), true, epub->getTitle());
  bookmarks_.load(*epub);
  initStats();

  updateRequired = true;
  lastAutoPageTurnTime = millis();
  bookLayoutAppliedOrientation_ = bookSettings.orientation;
  statusBarLayout_.markApplied(bookSettings);

  lastGoodSpineIndex_ = currentSpineIndex;
  lastGoodPageNumber_ = nextPageNumber;
  chapterRecoveryAttempted_ = false;
  invalidatePreparedPage();

  annUi_.clearSessionAndCapture();
}

bool EpubActivity::preventAutoSleep() { return SETTINGS.shakePageTurn != 0; }

int EpubActivity::wordAt(const int x, const int y) const {
  constexpr int padding = 8;
  for (size_t i = 0; i < touchWords_.size(); ++i) {
    const PageWordHit& word = touchWords_[i];
    if (x >= word.screenX - padding && x < word.screenX + word.screenW + padding && y >= word.screenY - padding &&
        y < word.screenY + word.screenH + padding) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void EpubActivity::closeWordSelection() {
  wordSelectionOpen_ = false;
  wordActionsOpen_ = false;
  selectedWord_ = -1;
  std::vector<PageWordHit>().swap(touchWords_);
}

std::vector<std::string> EpubActivity::currentWordActions() const {
  std::vector<std::string> actions = kBaseWordActions;
  if (selectedWord_ >= 0 && selectedWord_ < static_cast<int>(touchWords_.size()) &&
      !touchWords_[static_cast<size_t>(selectedWord_)].footnoteTarget.empty()) {
    actions.push_back("View footnote");
  }
  return actions;
}

void EpubActivity::renderWordSelection() {
  if (!wordSelectionOpen_ || selectedWord_ < 0 || selectedWord_ >= static_cast<int>(touchWords_.size())) {
    return;
  }

  // renderScreen() has already placed the reader page in the active buffer.
  // Patch that exact page so the selection overlay never brings back an older
  // reader frame from the second framebuffer.
  renderer.syncWriteBufferFromActive();
  renderer.setRenderMode(GfxRenderer::BW);

  const PageWordHit& word = touchWords_[static_cast<size_t>(selectedWord_)];
  renderer.ui.fillSparseInkLatticeInRect(word.screenX, std::max(0, word.screenY), std::max(1, word.screenW),
                                         std::max(3, word.screenH), 2);

  if (wordActionsOpen_) {
    const std::vector<std::string> actions = currentWordActions();
    const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(actions.size()));
    PopUp::background(renderer, box);
    PopUp::title(renderer, box, word.text);
    PopUp::list(renderer, box, actions, -1, 0);
    PopUp::border(renderer, box);
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

bool EpubActivity::openWordSelection(const int x, const int y) {
  if (!section || !epub) {
    return false;
  }

  const ViewportInfo info = calculateViewport();
  const int fontId = bookSettings.getReaderFontId();
  const int headerFontId = FontManager::getNextFont(fontId);
  auto page = section->loadPageFromSectionFile();
  if (!page) {
    return false;
  }

  touchWords_.clear();
  buildPageWordIndex(*page, renderer, fontId, headerFontId, info.totalMarginLeft, info.totalMarginTop, touchWords_);
  selectedWord_ = wordAt(x, y);
  INX_SERIAL.printf("[%lu] [WORD_SELECTION] open touch=(%d,%d) spine=%d page=%d word=%d\n", millis(), x, y,
                currentSpineIndex, section ? section->currentPage : -1, selectedWord_);
  if (selectedWord_ < 0) {
    std::vector<PageWordHit>().swap(touchWords_);
    return false;
  }

  wordSelectionOpen_ = true;
  wordActionsOpen_ = true;
  renderWordSelection();
  return true;
}

void EpubActivity::startVoiceNoteForSelection(const std::string& selectedText, const uint16_t wordLo,
                                              const uint16_t wordHi) {
#if !FREEINK_CAP_MIC
  // X4 Pro has no microphone. Use the shared keyboard entry instead and keep the note
  // attached to the selected annotation, just like the microphone path on Sticky.
  (void)wordLo;
  (void)wordHi;
  if (selectedText.empty()) {
    return;
  }
  enterNewActivity(new KeyboardEntryActivity(
      renderer, mappedInput, "Add note", "", 10, 256, false,
      [this](const std::string& note) {
        exitActivity();
        if (!note.empty() && annUi_.isActive()) {
          annUi_.setPendingNoteText(note);
        }
        updateRequired = true;
      },
      [this]() {
        exitActivity();
        updateRequired = true;
      }));
  return;
#else
  if (!epub || !section || selectedText.empty()) {
    return;
  }
  const std::string voiceDirectory = epub->getCachePath() + "/voice";
  enterNewActivity(new VoiceNoteActivity(
      renderer, mappedInput, voiceDirectory,
      [this](const std::string& audioPath, const bool success) {
        INX_SERIAL.printf("[%lu] [VOICE-NOTE] captured path=%s success=%d\n", millis(), audioPath.c_str(),
                          success ? 1 : 0);
        exitActivity();
        if (success && annUi_.isActive()) {
          annUi_.setPendingNoteAudioPath(audioPath);
        } else if (!success) {
          readerPopup("Could not record note");
        }
        updateRequired = true;
      },
      [this]() {
        exitActivity();
        updateRequired = true;
      }));
#endif  // FREEINK_CAP_MIC
}

bool EpubActivity::handleWordTouch() {
  if (!mappedInput.hasTouch()) {
    return false;
  }

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
    return false;
  }

  const int x = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int y = static_cast<int>(tapNy * renderer.getScreenHeight());
  if (mappedInput.lastTouchHeldMs() < wordSelectionHoldMs) {
    // Short taps remain reader navigation; a word action menu is intentional only on a long press.
    mappedInput.restoreTouchTapInScreen(renderer, tapNx, tapNy);
    return false;
  }

  INX_SERIAL.printf("[%lu] [WORD_SELECTION] long touch=(%d,%d) held=%lu\n", millis(), x, y,
                mappedInput.lastTouchHeldMs());
  if (!openWordSelection(x, y)) {
    // A long press outside text is intentionally consumed without opening an action menu.
    return false;
  }

  return true;
}

bool EpubActivity::handleTopRightTap() {
  if (!mappedInput.hasTouch()) {
    return false;
  }

  float tapX = 0.0f;
  float tapY = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) {
    return false;
  }

  // Keep held taps available for word selection and ordinary taps for page turns.
  if (mappedInput.lastTouchHeldMs() >= wordSelectionHoldMs ||
      tapX * renderer.getScreenWidth() < renderer.getScreenWidth() - 80 || tapY * renderer.getScreenHeight() >= 80) {
    mappedInput.restoreTouchTapInScreen(renderer, tapX, tapY);
    return false;
  }

  addBookmark();
  return true;
}

bool EpubActivity::handleImageTouch() {
  if (!section || !mappedInput.hasTouch()) {
    return false;
  }

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
    return false;
  }

  // Inline images frequently occupy the same screen halves used for page turns. Only a deliberate
  // hold opens the image viewer; ordinary taps go back through the reader's page-turn handler.
  if (mappedInput.lastTouchHeldMs() < wordSelectionHoldMs) {
    mappedInput.restoreTouchTapInScreen(renderer, tapNx, tapNy);
    return false;
  }

  const int x = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int y = static_cast<int>(tapNy * renderer.getScreenHeight());
  const ViewportInfo info = calculateViewport();
  auto page = section->loadPageFromSectionFile();
  std::string imagePath;
  int imageX = -1;
  int imageY = -1;
  int imageWidth = 0;
  int imageHeight = 0;
  if (!page || !page->imageAt(renderer, x, y, info.totalMarginLeft, info.totalMarginTop, imagePath, &imageX, &imageY,
                              &imageWidth, &imageHeight)) {
    mappedInput.restoreTouchTapInScreen(renderer, tapNx, tapNy);
    return false;
  }

  INX_SERIAL.printf("[%lu] [IMAGE_VIEWER] open touch=(%d,%d) path=%s\n", millis(), x, y, imagePath.c_str());
  pauseReadingStats();
  imageViewerUi_.open(*this, imagePath, imageX, imageY, imageWidth, imageHeight);
  return true;
}

bool EpubActivity::handleWordSelection() {
  if (!wordSelectionOpen_) {
    return false;
  }

  if (mappedInput.wasTouchSwipeUp() || mappedInput.wasTouchSwipeDown() || mappedInput.wasTouchSwipeLeft() ||
      mappedInput.wasTouchSwipeRight()) {
    closeWordSelection();
    renderScreen(true);
    return true;
  }

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
    return true;
  }

  const int x = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int y = static_cast<int>(tapNy * renderer.getScreenHeight());
  INX_SERIAL.printf("[%lu] [WORD_SELECTION] touch=(%d,%d) actions=%d spine=%d page=%d\n", millis(), x, y,
                wordActionsOpen_ ? 1 : 0, currentSpineIndex, section ? section->currentPage : -1);

  if (!wordActionsOpen_) {
    const int word = wordAt(x, y);
    if (word < 0) {
      closeWordSelection();
      renderScreen(true);
      return true;
    }
    selectedWord_ = word;
    wordActionsOpen_ = true;
    renderWordSelection();
    return true;
  }

  const std::vector<std::string> actions = currentWordActions();
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(actions.size()));
  const int listY = box.y + box.header;
  if (x < box.x || x >= box.x + box.width || y < listY || y >= listY + box.row * box.rows) {
    closeWordSelection();
    renderScreen(true);
    return true;
  }

  const int action = (y - listY) / box.row;
  INX_SERIAL.printf("[%lu] [WORD_SELECTION] action=%d spine=%d page=%d\n", millis(), action, currentSpineIndex,
                section ? section->currentPage : -1);
  const PageWordHit word = touchWords_[static_cast<size_t>(selectedWord_)];
  closeWordSelection();
  renderScreen(true);
  pauseReadingStats();

  if (action == 0) {
    if (!dictUi_.lookupAt(*this, word.screenX + word.screenW / 2, word.screenY + word.screenH / 2)) {
      renderScreen(true);
    }
  } else if (action == 1) {
    if (!annUi_.startAt(*this, word.screenX + word.screenW / 2, word.screenY + word.screenH / 2)) {
      renderScreen(true);
    }
  } else if (action == 2) {
    startVoiceNoteForPage();
  } else if (action == 3 && !word.footnoteTarget.empty()) {
    footnoteBody_.show(*this, word.footnoteTarget, word.text);
  }
  return true;
}

/**
 * @brief Called when exiting the activity
 */
void EpubActivity::onExit() {
  // NOTE: tap/swipe/held-touch discarding now happens centrally in switchTo() (main.cpp)
  // so every activity transition is covered, not just this one.
  invalidatePreparedPage();
  const bool clearImagePageOnExit = lastPageHadImages;
  closeWordSelection();
  resetPageNoteState();

  if (navigation_) {
    navigation_->reset();
  }

  if (settingsDrawer) {
    delete settingsDrawer;
    settingsDrawer = nullptr;
  }

  if (readingStats_.hasActivePageTimer()) {
    endPageTimer();
  }

  if (epub) {
    saveBookStats();

    if (section) {
      float spineProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
      float bookProgressValue = epub->calculateProgress(currentSpineIndex, spineProgress);
      RECENT_BOOKS.addBook(epub->getPath(), epub->getCachePath(), epub->getTitle(), epub->getAuthor(),
                           bookProgressValue);

      saveProgress(currentSpineIndex, section->currentPage, section->pageCount, false);
    }
  }

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.setDarkMode(SETTINGS.darkMode != 0);
  mappedInput.setInvertDirectionalAxes180(false);

  if (epub) {
    APP_STATE.lastRead = epub->getPath();
  }
  APP_STATE.saveToFile();
  section.reset();
  bookProgress.reset();
  statusBar.reset();
  epub.reset();

  renderer.resetTransientReaderState();



  
  const bool clearOnExit = clearImagePageOnExit || FREEINK_DEVICE_X4PRO;

  if (clearOnExit) {
    renderer.clearScreen(0xFF);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  FontManager::unloadAllSDFonts();

  ActivityWithSubactivity::onExit();
}

/**
 * @brief Main loop function called repeatedly while activity is active
 */
void EpubActivity::loop() {
  maybeCommitReadingSessionCount();

  if (subActivity) {
    subActivity->loop();
    consumePageNoteVoiceCompletion();
    return;
  }

  if (handlePageNoteOverlay()) {
    return;
  }

  if (wordSelectionOpen_) {
    handleWordSelection();
    return;
  }

  if (imageViewerUi_.isActive()) {
    imageViewerUi_.handleInput(*this);
    if (updateRequired && !imageViewerUi_.isActive()) {
      updateRequired = false;
      renderScreen(true);
    }
    return;
  }

  if (annUi_.isActive()) {
    annUi_.handleInput(*this);
    if (updateRequired && annUi_.isActive()) {
      updateRequired = false;
      annUi_.repaint(*this);
    } else if (updateRequired) {
      // handleInput may have called exit() (Save/Back): repaint() no-ops when inactive; must full compose reader.
      INX_SERIAL.printf("[%lu] [ANNOTATION] redraw after exit spine=%d page=%d\n", millis(), currentSpineIndex,
                    section ? section->currentPage : -1);
      updateRequired = false;
      renderScreen(true);
    }
    return;
  }

  if (dictUi_.isActive()) {
    dictUi_.handleInput(*this);
    if (updateRequired && dictUi_.isActive()) {
      updateRequired = false;
      dictUi_.repaint(*this);
    } else if (updateRequired) {
      updateRequired = false;
      renderScreen(true);
    }
    return;
  }

  if (footnoteBody_.isActive()) {
    footnoteBody_.handleInput(*this);
    if (updateRequired && footnoteBody_.isActive()) {
      updateRequired = false;
      footnoteBody_.repaint(*this);
    } else if (updateRequired) {
      updateRequired = false;
      renderScreen(true);
    }
    return;
  }

  if (orientationPicker_.isActive()) {
    orientationPicker_.handleInput(*this);
    // handleInput() already repaints inline (popup redraw on Up/Down, renderScreen(true) on Back) -
    // Confirm instead sets isToggleClosed so the block below performs the actual relayout next loop().
    return;
  }

  if (presetPicker_.isActive()) {
    presetPicker_.handleInput(*this);
    return;
  }

  if (quickActionsUi_.isActive()) {
    quickActionsUi_.handleInput(*this);
    return;
  }

  if (goToPercentUi_.isActive()) {
    goToPercentUi_.handleInput(*this);
    return;
  }

  if (navigation_ && navigation_->handleInput(mappedInput)) {
    return;
  }

  if (settingsDrawerVisible && settingsDrawer) {
    settingsDrawer->handleInput(mappedInput);
    if (settingsDrawer->isDismissed()) {
      saveBookSettings();
      settingsDrawerVisible = false;
      vTaskDelay(pdMS_TO_TICKS(100));
      isToggleClosed = true;
      suppressBackUntilReleased_ = true;
      updateRequired = true;
      lastAutoPageTurnTime = millis();
    } else if (updateRequired) {
      // Dark mode changes only the renderer's visual output. Redraw the current page immediately,
      // without rebuilding its layout or pagination.
      updateRequired = false;
      renderScreen(true);
    }
    return;
  }

  if (isToggleClosed) {
    isToggleClosed = false;
    const bool inheritedSettingsChanged = syncSettingsFromGlobalIfNeeded();
    const bool layoutNeedsRebuild = inheritedSettingsChanged || statusBarLayout_.changedSinceApplied(bookSettings) ||
                                    (settingsDrawer && settingsDrawer->shouldUpdate()) ||
                                    (bookSettings.orientation != bookLayoutAppliedOrientation_);
    if (layoutNeedsRebuild) {
      // Remove the drawer from the displayed frame before the layout rebuild starts. The rebuild
      // shows its own progress UI and must never compose it over the stale settings panel.
      if (settingsDrawer) settingsDrawer->hide();
      settingsDrawerVisible = false;
      renderScreen(true);
      applyBookSettings();
      if (settingsDrawer) {
        settingsDrawer->clearUpdateFlag();
      }
    } else {
      setupOrientation();
      bookLayoutAppliedOrientation_ = bookSettings.orientation;
    }
    startPageTimer();
    return;
  }

  if (suppressBackUntilReleased_) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
      return;
    }
    suppressBackUntilReleased_ = false;
  }

  if (section && epub && !settingsDrawerVisible) {
    annUi_.tryChordEnter(*this);
    dictUi_.tryChordEnter(*this);
  }

  // A chord above may have just opened the annotation/dictionary overlay this same frame, with
  // Down+Right/Down+Left still physically held. Both overlays already reset btnBindings_'s
  // press-state in their enter(), but calling handleInput() below would immediately re-observe
  // those still-held buttons and re-arm them - freezing a "held" state for the whole overlay
  // session that goes stale by the time it closes (same bug the reset() was meant to prevent, just
  // deferred by one frame). Skip it entirely once an overlay has taken over input this frame.
  if (annUi_.isActive() || dictUi_.isActive()) {
    return;
  }

#if FREEINK_DEVICE_X4PRO
  // Edge bands adjust the frontlight; a middle swipe falls through to close/settings below.
  if (frontlight_ui::handleEdgeSwipe(mappedInput, renderer)) {
    return;
  }
#endif

  // On the touch reader, an upward swipe from the reading view closes the book.  SettingsDrawer gets
  // first chance to consume the same gesture while it is open, so its list can still paginate.
  if (mappedInput.wasTouchSwipeUpForRenderer(renderer)) {
    pauseReadingStats();
    // Same settle delay the Back button uses below. Without it the exit repaint composes on
    // top of a still-settling panel refresh and the page ghosts through; Back never showed it
    // because it already waited here.
    vTaskDelay(pdMS_TO_TICKS(100));
    onGoBack();
    return;
  }

  // On the touch reader, a downward swipe is the direct gesture for book settings. Handle it before the
  // configurable Down-button mapping so it cannot turn the page or trigger another reader action.
  if (mappedInput.wasTouchSwipeDownForRenderer(renderer)) {
    pauseReadingStats();
    toggleSettingsDrawer();
    lastAutoPageTurnTime = millis();
    return;
  }

  const bool readerTouchEnabled = !settingsDrawer || settingsDrawer->isTouchEnabled();

  if (readerTouchEnabled && mappedInput.wasTouchSwipeRightForRenderer(renderer)) {
    pauseReadingStats();
    openTableOfContents();
    return;
  }

  if (readerTouchEnabled && handlePageNoteTouch()) {
    return;
  }

  if (readerTouchEnabled && handleTopRightTap()) {
    return;
  }

  if (readerTouchEnabled && handleImageTouch()) {
    return;
  }

  if (readerTouchEnabled && handleWordTouch()) {
    return;
  }

  // Physical page-button handling is fixed in ReaderButtonBindings: short press turns a page and long
  // press skips a chapter. Touch UI owns the reader tools and settings.
  if (btnBindings_.handleInput(*this)) {
    return;
  }

  // Power (short-press only, no long-press pairing) shares the same READER_BUTTON_ACTION dispatcher
  // as Quick Actions - see READER_SETTINGS.btnPowerShortAction's doc comment for why this supersedes
  // the old 4-option readerShortPwrBtn.
  if (mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    btnBindings_.dispatch(*this, READER_SETTINGS.btnPowerShortAction);
    return;
  }

  const MappedInputManager::MotionGesture motionGesture = mappedInput.readMotionGesture(
      static_cast<uint8_t>(renderer.getOrientation()), SETTINGS.shakePageTurn, SETTINGS.shakePageTurnSensitivity);
  if (motionGesture != MappedInputManager::MotionGesture::None) {
    endPageTimer();
    pageTurn(motionGesture == MappedInputManager::MotionGesture::Next);
    lastAutoPageTurnTime = millis();
    return;
  }

  if (READER_SETTINGS.pageAutoTurnSeconds > 0 && (!navigation_ || !navigation_->isTocOpen()) && !settingsDrawerVisible) {
    if (lastAutoPageTurnTime == 0) {
      lastAutoPageTurnTime = millis();
    }

    unsigned long elapsed = millis() - lastAutoPageTurnTime;
    if (elapsed >= (READER_SETTINGS.pageAutoTurnSeconds * 1000UL)) {
      lastAutoPageTurnTime = millis();
      endPageTimer();
      (void)pageTurn(true);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= bookmarkHoldMs) {
    pauseReadingStats();
    addBookmark();
    startPageTimer();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    pauseReadingStats();
    vTaskDelay(pdMS_TO_TICKS(100));
    onGoBack();
    return;
  }

  if (updateRequired) {
    updateRequired = false;
    renderScreen();
    return;
  }

  // Compose one forward page only after the current page has been stable for a
  // short idle window. The panel keeps displaying the active framebuffer while
  // the inactive one is prepared; no display transaction occurs here.
  runIdlePreparedPage();
  runIdleSecondNextPage();
}

/**
 * @brief Opens the Table of Contents sidebar.
 */
void EpubActivity::openTableOfContents() {
  if (navigation_) navigation_->openTableOfContents();
}

/**
 * @brief Toggles the settings drawer visibility
 */
void EpubActivity::toggleSettingsDrawer() {
  if (!settingsDrawer) {
    settingsDrawer = new SettingsDrawer(renderer, bookSettings, [this]() {
      const bool darkMode = bookSettings.darkMode != 0;
      if (renderer.isDarkMode() != darkMode) {
        renderer.setDarkMode(darkMode);
        updateRequired = true;
      }
      onBookSettingsLiveLayoutSync();
    });
  }

  settingsDrawerVisible = !settingsDrawerVisible;

  if (settingsDrawerVisible) {
    invalidatePreparedPage();
    pauseReadingStats();
    syncSettingsFromGlobalIfNeeded();
    settingsDrawerSnapshot_ = bookSettings;
    hasSettingsDrawerSnapshot_ = true;

    settingsDrawer->show();
    return;
  }
}

void EpubActivity::invalidatePreparedPage() {
  preparedPage_ = PreparedPage{};
  preparedPageSpineIndex_ = -1;
  preparedPageIndex_ = -1;
  preparePageAfterMs_ = 0;
  preloadedPageSpineIndex_ = -1;
  preloadedPageIndex_ = -1;
}

uint32_t EpubActivity::preparedPageRenderSignature() {
  const ViewportInfo info = calculateViewport();
  uint32_t hash = 2166136261u;
  hash = addPreparedPageSignature(hash, static_cast<uint32_t>(renderer.getOrientation()));
  hash = addPreparedPageSignature(hash, static_cast<uint32_t>(info.totalMarginTop));
  hash = addPreparedPageSignature(hash, static_cast<uint32_t>(info.totalMarginRight));
  hash = addPreparedPageSignature(hash, static_cast<uint32_t>(info.totalMarginBottom));
  hash = addPreparedPageSignature(hash, static_cast<uint32_t>(info.totalMarginLeft));
  hash = addPreparedPageSignature(hash, static_cast<uint32_t>(info.width));
  hash = addPreparedPageSignature(hash, static_cast<uint32_t>(info.height));
  hash = addPreparedPageSignature(hash, static_cast<uint32_t>(info.fontId));
  hash = addPreparedPageSignature(hash, static_cast<uint32_t>(FontManager::getNextFont(info.fontId)));
  hash = addPreparedPageSignature(hash, floatBits(info.lineCompression));
  hash = addPreparedPageSignature(hash, floatBits(info.wordSpacing));
  hash = addPreparedPageSignature(hash, StatusBar::layoutSignature(bookSettings));
  hash = addPreparedPageSignature(hash, static_cast<uint32_t>(bookSettings.readingGuideLinesEnabled));
  hash = addPreparedPageSignature(hash, static_cast<uint32_t>(READER_SETTINGS.readerImageGrayscale));
  hash = addPreparedPageSignature(hash, static_cast<uint32_t>(READER_SETTINGS.textAntiAliasing));
  hash = addPreparedPageSignature(hash, static_cast<uint32_t>(READER_SETTINGS.getRefreshFrequency()));
  return hash;
}

bool EpubActivity::canPrepareForwardPage() const {
  if (!epub || !section || section->pageCount <= 0 || section->currentPage < 0 ||
      section->currentPage + 1 >= section->pageCount) {
    return false;
  }

  // Stored highlights/page notes are overlaid through live annotation geometry.
  // Other annotated pages do not matter; only hold back a prepared frame when
  // its exact forward target has an overlay we would otherwise omit.
  const int targetPage = section->currentPage + 1;
  if (std::any_of(annUi_.annotations().records().begin(), annUi_.annotations().records().end(),
                  [this, targetPage](const EpubAnnotationRecord& record) {
                    return EpubAnnotations::recordTouchesPage(record, currentSpineIndex, targetPage);
                  })) {
    return false;
  }

  return (!navigation_ || !navigation_->isTocOpen()) && !settingsDrawerVisible && !wordSelectionOpen_ &&
         !pageNotePopupOpen_ && !annUi_.isActive() && !dictUi_.isActive() && !imageViewerUi_.isActive() &&
         !orientationPicker_.isActive() && !presetPicker_.isActive() && !quickActionsUi_.isActive() && !subActivity;
}

void EpubActivity::scheduleIdlePreparedPage() {
  invalidatePreparedPage();
  if (!canPrepareForwardPage()) {
    return;
  }

  preparedPageSpineIndex_ = currentSpineIndex;
  preparedPageIndex_ = section->currentPage + 1;
  preparePageAfterMs_ = millis() + 750;
}

bool EpubActivity::composePreparedForwardPage() {
  if (!canPrepareForwardPage() || preparedPageSpineIndex_ != currentSpineIndex ||
      preparedPageIndex_ != section->currentPage + 1) {
    return false;
  }

  const int savedPage = section->currentPage;
  section->currentPage = preparedPageIndex_;
  Page* page = section->loadPageFromSectionFile();
  if (!page) {
    section->currentPage = savedPage;
    return false;
  }

  const ViewportInfo info = calculateViewport();
  const int fontId = bookSettings.getReaderFontId();
  const int headerFontId = FontManager::getNextFont(fontId);
  if (!FontManager::ensureReaderLayoutFonts(fontId, renderer)) {
    section->currentPage = savedPage;
    return false;
  }

  const bool pageHasImages = page->hasImages();
  const bool textAa = READER_SETTINGS.textAntiAliasing != 0 && renderer.text.supportsAntiAliasing(fontId);
  const bool readerImageTwoBit = READER_SETTINGS.readerImageGrayscale != 0 && pageHasImages;
  const bool highImageMode = READER_SETTINGS.readerImageGrayscale == SystemSetting::READER_IMAGE_HIGH && pageHasImages;
  const bool highQuality = highImageMode && page->anyImageNeedsGrayscale();
  const bool mediumImageGrayscale =
      (READER_SETTINGS.readerImageGrayscale == SystemSetting::READER_IMAGE_MEDIUM && page->hasNonPngImages()) ||
      (highImageMode && !highQuality && textAa);
  const bool needsImageGrayscale = mediumImageGrayscale || highQuality;
  const ImageRenderMode imageMode = readerImageTwoBit ? ImageRenderMode::TwoBit : ImageRenderMode::OneBit;
  const bool skipImagesInBase = needsImageGrayscale && highQuality;

  // This is the framebuffer that is not on the panel. Keep this compositor
  // deliberately display-free: the current page remains visible throughout.
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen(0xFF);
  page->render(renderer, fontId, headerFontId, info.totalMarginLeft, info.totalMarginTop, skipImagesInBase, imageMode,
               /*skipOnlyGrayscaleImages=*/highQuality);
  ReadingGuideLines::render(renderer, *page, bookSettings.readingGuideLinesEnabled, info.totalMarginTop,
                            info.totalMarginRight, info.totalMarginBottom, info.totalMarginLeft, fontId);
  renderStatusBar(info.totalMarginRight, info.totalMarginBottom, info.totalMarginLeft);
  if (isCurrentPageBookmarked()) {
    drawBookmarkIndicator();
  }

  // The normal high-quality renderer shows this placeholder only while a
  // two-bit display cache is still cold. Match that base frame precisely; the
  // quality pass on the actual turn replaces it with the real image.
  const bool highQualityCacheReady =
      highQuality && page->allGrayscaleImagesCachedTwoBit(renderer, info.totalMarginLeft, info.totalMarginTop,
                                                          /*quality=*/true);
  const bool imagePlaceholder = highQuality && !highQualityCacheReady;
  if (imagePlaceholder) {
    page->fillImageRects(renderer, info.totalMarginLeft, info.totalMarginTop, true, /*onlyGrayscale=*/true);
  }

  const bool hasImages = pageHasImages;
  const bool hasLargeImage =
      hasImages && pageImageFootprintAtLeastHalfScreen(*page, renderer, info.totalMarginLeft, info.totalMarginTop);
  const int refreshFrequency = READER_SETTINGS.getRefreshFrequency();
  const bool smartImageRefreshEnabled =
      READER_SETTINGS.readerImageGrayscale == SystemSetting::READER_IMAGE_HIGH && !isBookmarking && !annUi_.isActive();
  const bool smartRefreshAfterQualityImage = lastPageHadHighQualityImage;
  const bool smartRefreshAfterLargeImage = lastPageHadImages && lastPageHadLargeImage;
  const bool forceHalfRefresh =
      smartRefreshAfterQualityImage || (smartImageRefreshEnabled && smartRefreshAfterLargeImage) ||
      (refreshFrequency > 0 && pagesUntilFullRefresh <= 1);

  preparedPage_.ready = true;
  preparedPage_.spineIndex = currentSpineIndex;
  preparedPage_.pageIndex = preparedPageIndex_;
  preparedPage_.renderSignature = preparedPageRenderSignature();
  preparedPage_.refreshMode = static_cast<uint8_t>(forceHalfRefresh ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  preparedPage_.pagesUntilFullRefresh =
      forceHalfRefresh ? refreshFrequency : (refreshFrequency > 0 ? pagesUntilFullRefresh - 1 : pagesUntilFullRefresh);
  preparedPage_.hasImages = hasImages;
  preparedPage_.hasLargeImage = hasLargeImage;
  preparedPage_.hasHighQualityImage = highQuality;
  preparedPage_.highQuality = highQuality;
  preparedPage_.mediumImageGrayscale = mediumImageGrayscale;
  preparedPage_.textAntiAlias = textAa;
  preparedPage_.imagePlaceholder = imagePlaceholder;
  section->currentPage = savedPage;

  INX_SERIAL.printf("[%lu] [EPA-PREP] ready spine=%d page=%d images=%d refresh=%d\n", millis(),
                    preparedPage_.spineIndex, preparedPage_.pageIndex, hasImages ? 1 : 0,
                    static_cast<int>(preparedPage_.refreshMode));
  return true;
}

void EpubActivity::runIdlePreparedPage() {
  if (preparedPage_.ready || preparedPageSpineIndex_ < 0 || preparedPageIndex_ < 0 ||
      millis() < preparePageAfterMs_) {
    return;
  }

  if (!composePreparedForwardPage()) {
    invalidatePreparedPage();
    return;
  }

  // The Section cache keeps previous/current/next serialized pages in PSRAM;
  // do not chain more work after the one immediate forward page.
  preparePageAfterMs_ = 0;
  yield();
}

void EpubActivity::runIdleSecondNextPage() {
  // The inactive framebuffer already contains currentPage + 1. Keep only the
  // following page's serialized layout in the fourth PSRAM page-cache slot:
  // no third framebuffer, image decode, or display-cache work happens here.
  if (!section || !preparedPage_.ready || preparedPage_.spineIndex != currentSpineIndex ||
      preparedPage_.pageIndex != section->currentPage + 1) {
    return;
  }

  const int targetPage = section->currentPage + 2;
  if (targetPage < 0 || targetPage >= section->pageCount ||
      (preloadedPageSpineIndex_ == currentSpineIndex && preloadedPageIndex_ == targetPage)) {
    return;
  }

  const bool cached = section->preloadPage(targetPage);
  preloadedPageSpineIndex_ = currentSpineIndex;
  preloadedPageIndex_ = targetPage;
  INX_SERIAL.printf("[%lu] [EPA-PREP] page-cache spine=%d page=%d ok=%d\n", millis(), currentSpineIndex,
                    targetPage, cached ? 1 : 0);
  yield();
}

bool EpubActivity::finishPreparedPageGrayscale(const PreparedPage& prepared) {
  if (!section || section->currentPage != prepared.pageIndex) {
    return false;
  }

  Page* page = section->loadPageFromSectionFile();
  if (!page) {
    return false;
  }

  const ViewportInfo info = calculateViewport();
  const int fontId = bookSettings.getReaderFontId();
  const int headerFontId = FontManager::getNextFont(fontId);
  const bool pageHasImages = page->hasImages();
  const bool textAa = READER_SETTINGS.textAntiAliasing != 0 && renderer.text.supportsAntiAliasing(fontId);
  const bool readerImageTwoBit = READER_SETTINGS.readerImageGrayscale != 0 && pageHasImages;
  const bool highImageMode = READER_SETTINGS.readerImageGrayscale == SystemSetting::READER_IMAGE_HIGH && pageHasImages;
  const bool highQuality = highImageMode && page->anyImageNeedsGrayscale();
  const bool mediumImageGrayscale =
      (READER_SETTINGS.readerImageGrayscale == SystemSetting::READER_IMAGE_MEDIUM && page->hasNonPngImages()) ||
      (highImageMode && !highQuality && textAa);
  const ImageRenderMode imageMode = readerImageTwoBit ? ImageRenderMode::TwoBit : ImageRenderMode::OneBit;

  // Settings are included in the prepared-frame signature. Check the derived
  // render decisions too, so a future setting added to the signature cannot
  // accidentally apply a different grayscale sequence to this base frame.
  if (prepared.highQuality != highQuality || prepared.mediumImageGrayscale != mediumImageGrayscale ||
      prepared.textAntiAlias != textAa) {
    return false;
  }

  if (!highQuality && !mediumImageGrayscale && !textAa) {
    return true;
  }

  // `displayBuffer()` just swapped the prepared BW base to the active buffer.
  // Recreate the normal renderer's pre-display BW snapshot from that exact
  // frame before asking the controller to run a grayscale pass.
  renderer.syncWriteBufferFromActive();
  const bool bwStored = renderer.storeBwBuffer();
  if (!bwStored) {
    return false;
  }

  if (highQuality) {
    ImageRender::displayGrayscale(
        renderer, /*quality=*/true, /*preserveText=*/true,
        [&] {
          renderer.copyStoredBwToFramebuffer();
          renderer.invertScreen();
          page->fillImageRects(renderer, info.totalMarginLeft, info.totalMarginTop, false, /*onlyGrayscale=*/true);
          page->renderImages(renderer, fontId, info.totalMarginLeft, info.totalMarginTop, imageMode, /*quality=*/true,
                             /*onlyGrayscale=*/true);
        },
        kReaderHighQualityFastLut);
    // X4 Pro cannot run an AA pass over a freshly rendered high-quality image without
    // washing it out — its AA bank drives all four channels, so pixels the pass never
    // repaints (skipImages=true) get driven anyway. Skip the pass there; every other board
    // runs it exactly as before.
#if FREEINK_DEVICE_X4PRO
    constexpr bool kAaAfterHqImage = false;
#else
    constexpr bool kAaAfterHqImage = true;
#endif
    if (textAa && kAaAfterHqImage) {
      const bool textBwStored = renderer.storeBwBuffer();
      if (textBwStored) {
        renderer.renderGrayscalePasses(/*quality=*/false, /*preserveText=*/true, [&] {
          renderer.clearScreen(0x00);
          page->render(renderer, fontId, headerFontId, info.totalMarginLeft, info.totalMarginTop,
                       /*skipImages=*/true, ImageRenderMode::OneBit);
        });
      }
    }
  } else if (textAa && !mediumImageGrayscale) {
    renderer.renderGrayscalePasses(/*quality=*/false, /*preserveText=*/true, [&] {
      renderer.clearScreen(0x00);
      page->render(renderer, fontId, headerFontId, info.totalMarginLeft, info.totalMarginTop,
                   /*skipImages=*/false, ImageRenderMode::OneBit);
    });
  } else {
    ImageRender::displayGrayscale(renderer, /*quality=*/false, /*preserveText=*/true, [&] {
      renderer.clearScreen(0x00);
      if (textAa) {
        page->render(renderer, fontId, headerFontId, info.totalMarginLeft, info.totalMarginTop,
                     /*skipImages=*/true, ImageRenderMode::OneBit);
      }
      if (mediumImageGrayscale) {
        page->renderImages(renderer, fontId, info.totalMarginLeft, info.totalMarginTop, imageMode);
      }
    });
  }

  if (highQuality) {
    // Keep both host framebuffers and the controller's RED baseline on the
    // exact B/W base that was displayed before the quality image overlay.
    renderer.clearScreen();
    page->render(renderer, fontId, headerFontId, info.totalMarginLeft, info.totalMarginTop, /*skipImages=*/true,
                 ImageRenderMode::OneBit, /*skipOnlyGrayscaleImages=*/true);
    ReadingGuideLines::render(renderer, *page, bookSettings.readingGuideLinesEnabled, info.totalMarginTop,
                              info.totalMarginRight, info.totalMarginBottom, info.totalMarginLeft, fontId);
    renderStatusBar(info.totalMarginRight, info.totalMarginBottom, info.totalMarginLeft);
    if (isCurrentPageBookmarked()) {
      drawBookmarkIndicator();
    }
    page->fillImageRects(renderer, info.totalMarginLeft, info.totalMarginTop, false, /*onlyGrayscale=*/true);
    renderer.cleanupGrayscaleWithFrameBuffer();
  }

  return true;
}

bool EpubActivity::presentPreparedForwardPage() {
  if (!preparedPage_.ready || !canPrepareForwardPage() || !section || preparedPage_.spineIndex != currentSpineIndex ||
      preparedPage_.pageIndex != section->currentPage + 1 ||
      preparedPage_.renderSignature != preparedPageRenderSignature()) {
    return false;
  }

  const PreparedPage presented = preparedPage_;
  section->currentPage = presented.pageIndex;
  // Async: the buffer swap (and so the write buffer this function/finishPreparedPageGrayscale() draws
  // into next) is already done by the time this returns - only the panel's own waveform still runs in
  // the background. Every StickyDisplay call that actually needs the refresh finished (grayscale passes,
  // the next page's displayBuffer()) already waits for it first via finish(), so this does not skip any
  // required wait - it just stops blocking the common plain-text turn on the ~600ms e-ink refresh.
  // If the page being replaced had a high-quality image, this turn does a HALF refresh, full
  // stop. The prepared page's own refreshMode cannot decide this: prepareNextPage() runs BEFORE
  // the previous page is presented, so it reads lastPageHadHighQualityImage while it is still
  // stale. Decide it here, where the flag is current.
  // X4 Pro takes a FULL refresh here, not Half: neither Half nor Fast clears what a quality
  // pass leaves on these panels. Other devices keep Half.
  const HalDisplay::RefreshMode turnRefresh =
      lastPageHadHighQualityImage
          ? (FREEINK_DEVICE_X4PRO ? HalDisplay::FULL_REFRESH : HalDisplay::HALF_REFRESH)
          : static_cast<HalDisplay::RefreshMode>(presented.refreshMode);
  renderer.displayBufferAsync(turnRefresh);

  if (!finishPreparedPageGrayscale(presented)) {
    // The base is already visible. Fall back to the established full renderer
    // from this exact page rather than risking a mismatched grayscale baseline.
    invalidatePreparedPage();
    renderScreen();
    return true;
  }

  pagesUntilFullRefresh = presented.pagesUntilFullRefresh;
  lastPageHadImages = presented.hasImages;
  lastPageHadLargeImage = presented.hasLargeImage;
  lastPageHadHighQualityImage = presented.hasHighQualityImage;
  saveProgress(currentSpineIndex, section->currentPage, section->pageCount, false);
  lastGoodSpineIndex_ = currentSpineIndex;
  lastGoodPageNumber_ = section->currentPage;
  chapterRecoveryAttempted_ = false;
  updateRequired = false;
  invalidatePreparedPage();
  scheduleIdlePreparedPage();

  INX_SERIAL.printf("[%lu] [EPA-PREP] displayed spine=%d page=%d\n", millis(), currentSpineIndex,
                    section->currentPage);
  return true;
}

void EpubActivity::jumpToPercent(int percent) {
  if (!epub) {
    return;
  }

  const size_t bookSize = epub->getBookSize();
  if (bookSize == 0) {
    return;
  }

  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  size_t targetSize =
      (bookSize / 100) * static_cast<size_t>(percent) + (bookSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) {
    targetSize = bookSize - 1;
  }

  const int spineCount = epub->getSpineItemsCount();
  if (spineCount == 0) {
    return;
  }

  int targetSpineIndex = spineCount - 1;
  size_t prevCumulative = 0;

  for (int i = 0; i < spineCount; i++) {
    const size_t cumulative = epub->getCumulativeSpineItemSize(i);
    if (targetSize <= cumulative) {
      targetSpineIndex = i;
      prevCumulative = (i > 0) ? epub->getCumulativeSpineItemSize(i - 1) : 0;
      break;
    }
  }

  const size_t cumulative = epub->getCumulativeSpineItemSize(targetSpineIndex);
  const size_t spineSize = (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;
  pendingSpineProgress =
      (spineSize == 0) ? 0.0f : static_cast<float>(targetSize - prevCumulative) / static_cast<float>(spineSize);
  if (pendingSpineProgress < 0.0f) {
    pendingSpineProgress = 0.0f;
  } else if (pendingSpineProgress > 1.0f) {
    pendingSpineProgress = 1.0f;
  }

  currentSpineIndex = targetSpineIndex;
  nextPageNumber = 0;
  pendingPercentJump = true;
  section.reset();
}

/**
 * @brief Handles page turning logic
 * @param forward True for forward page turn, false for backward
 */
bool EpubActivity::pageTurn(bool forward) {
  INX_SERIAL.printf("[%lu] [EPA] pageTurn forward=%d before spine=%d page=%d next=%d\n", millis(), forward ? 1 : 0,
                currentSpineIndex, section ? section->currentPage : -1, nextPageNumber);
  if (!epub) {
    updateRequired = true;
    return false;
  }

  if (!section) {
    updateRequired = true;
    return false;
  }

  if (section->pageCount == 0) {
    section.reset();
    updateRequired = true;
    return false;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    section->currentPage = 0;
  }

  // A prepared frame is valid only for this exact forward neighbour. Presenting
  // it skips all page layout, image decode, font, and SD work on the turn.
  if (forward && presentPreparedForwardPage()) {
    startPageTimer();
    return true;
  }

  // Any other navigation leaves the inactive framebuffer available for normal
  // rendering; it must not later be mistaken for the old forward neighbour.
  invalidatePreparedPage();

  bool needSectionReset = false;
  int newSpineIndex = currentSpineIndex;
  int newNextPageNumber = nextPageNumber;

  if (forward) {
    if (section->currentPage < section->pageCount - 1) {
      section->currentPage++;
    } else {
      int totalSpines = epub->getSpineItemsCount();
      if (currentSpineIndex < totalSpines - 1) {
        readingStats_.addChapterRead();
        newSpineIndex = currentSpineIndex + 1;
        newNextPageNumber = 0;
        needSectionReset = true;
      } else {
        newSpineIndex = totalSpines;
        needSectionReset = true;
      }
    }
  } else {
    if (section->currentPage > 0) {
      section->currentPage--;
    } else if (currentSpineIndex > 0) {
      newSpineIndex = currentSpineIndex - 1;
      newNextPageNumber = UINT16_MAX;
      needSectionReset = true;
    }
  }

  if (needSectionReset) {
    currentSpineIndex = newSpineIndex;
    nextPageNumber = newNextPageNumber;
    section.reset();
  }

  startPageTimer();
  updateRequired = true;
  INX_SERIAL.printf("[%lu] [EPA] pageTurn after spine=%d page=%d next=%d reload=%d\n", millis(), currentSpineIndex,
                section ? section->currentPage : -1, nextPageNumber, needSectionReset ? 1 : 0);
  return false;
}

/**
 * @brief Renders the current screen content
 */
void EpubActivity::renderScreen(const bool clearFramebuffer) {
  // All normal rendering overwrites the writable framebuffer, so discard a
  // pending page before entering any slow/current-page render path.
  invalidatePreparedPage();
  if (!epub) return;

  int totalSpine = epub->getSpineItemsCount();
  if (totalSpine <= 0) {
    return;
  }

  if (currentSpineIndex >= totalSpine) {
    renderer.clearScreen(0xFF);
    displayBookStats();
    BOOK_STATE.setFinished(epub->getPath(), true);
    return;
  }

  if (currentSpineIndex < 0 || currentSpineIndex >= totalSpine) {
    currentSpineIndex = 0;
    nextPageNumber = 0;
    section.reset();
  }

  ViewportInfo info = calculateViewport();

  if (!section) {
    const bool wasLayoutReload = suppressNextSectionLoadProgress_;
    const bool showSectionLoadProgress = !wasLayoutReload;
    suppressNextSectionLoadProgress_ = false;
    loadCurrentSection(showSectionLoadProgress);
    if (!section) {
      INX_SERIAL.printf("[%lu] [EPA] renderScreen: loadCurrentSection failed spine=%d total=%d\n", millis(),
                    currentSpineIndex, totalSpine);
      if (currentSpineIndex >= totalSpine) {
        renderer.clearScreen(0xFF);
        displayBookStats();
        BOOK_STATE.setFinished(epub->getPath(), true);
        return;
      }
      if (wasLayoutReload) {
        readerPopup("Error updating layout");
        return;
      }
      handleChapterLoadFailure();
      return;
    }

    if (pendingPercentJump && section->pageCount > 0) {
      int newPage = static_cast<int>(pendingSpineProgress * static_cast<float>(section->pageCount));
      if (newPage >= section->pageCount) {
        newPage = section->pageCount - 1;
      }
      section->currentPage = newPage;
      pendingPercentJump = false;
    }
  }

  if (clearFramebuffer) {
    renderer.clearScreen(0xFF);
  }

  if (section->pageCount == 0) {
    INX_SERIAL.printf("[%lu] [EPA] renderScreen: zero page section spine=%d total=%d\n", millis(), currentSpineIndex,
                  totalSpine);
    section.reset();
    handleChapterLoadFailure();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    section->currentPage = 0;
  }

  auto page = section->loadPageFromSectionFile();
  if (!page) {
    INX_SERIAL.printf("[%lu] [EPA] renderScreen: page deserialize failed spine=%d page=%d count=%u\n", millis(),
                  currentSpineIndex, section->currentPage, static_cast<unsigned>(section->pageCount));
    section->clearCache();
    section.reset();
    handleChapterLoadFailure();
    return;
  }

  renderContents(page, info.totalMarginTop, info.totalMarginRight, info.totalMarginBottom, info.totalMarginLeft);

  if (settingsDrawerVisible && settingsDrawer) settingsDrawer->render();
  if (navigation_) navigation_->render();

  saveProgress(currentSpineIndex, section->currentPage, section->pageCount, false);
  lastGoodSpineIndex_ = currentSpineIndex;
  lastGoodPageNumber_ = section->currentPage;
  chapterRecoveryAttempted_ = false;
  scheduleIdlePreparedPage();
}

/**
 * @brief Renders the page contents with margins and status bar
 * @param page Page to render
 * @param orientedMarginTop Top margin
 * @param orientedMarginRight Right margin
 * @param orientedMarginBottom Bottom margin
 * @param orientedMarginLeft Left margin
 */
void EpubActivity::renderContents(Page* page, const int orientedMarginTop,
                                  const int orientedMarginRight, const int orientedMarginBottom,
                                  const int orientedMarginLeft) {
  if (!page) return;
  const int fontId = bookSettings.getReaderFontId();
  FontManager::ensureReaderLayoutFonts(fontId, renderer);
  const int headerFontId = FontManager::getNextFont(fontId);
  const bool pageHasImages = page->hasImages();

  annUi_.ensureDiskListLoaded(*this);

  bool needAnnotationGeometry = annUi_.isActive();
  if (!annUi_.isActive() && !annUi_.annotations().records().empty()) {
    needAnnotationGeometry = std::any_of(annUi_.annotations().records().begin(), annUi_.annotations().records().end(),
                                         [this](const EpubAnnotationRecord& rec) {
                                           return section && EpubAnnotations::recordTouchesPage(rec, currentSpineIndex,
                                                                                                section->currentPage);
                                         });
  }

  bool omitStoredWordStrings = false;
  if (needAnnotationGeometry && !annUi_.isActive()) {
    omitStoredWordStrings = true;
    for (const EpubAnnotationRecord& rec : annUi_.annotations().records()) {
      if (!section || !EpubAnnotations::recordTouchesPage(rec, currentSpineIndex, section->currentPage)) {
        continue;
      }
      // A wildcard index always needed the word text for its search fallback. A stored phrase (rec.text)
      // now also needs it: mergeStoredRangesForPage() verifies a precise index against that phrase before
      // trusting it (a font/layout change repaginates the book, so a stale index can otherwise land on the
      // wrong words entirely) and falls back to searching by phrase when it doesn't match - both need the
      // actual word strings to compare against, not just the index.
      if (rec.pageWordLo == EpubAnnotations::kWildcard || !rec.text.empty()) {
        omitStoredWordStrings = false;
        break;
      }
    }
  }

  const bool wordIndexCacheHit =
      needAnnotationGeometry && section != nullptr && annUi_.wordIndexCacheSpine() == currentSpineIndex &&
      annUi_.wordIndexCachePage() == section->currentPage && annUi_.wordIndexCacheFontId() == fontId &&
      annUi_.wordIndexCacheHeaderFontId() == headerFontId && annUi_.wordIndexCacheMarginL() == orientedMarginLeft &&
      annUi_.wordIndexCacheMarginT() == orientedMarginTop;

  if (!needAnnotationGeometry) {
    annUi_.words().clear();
    annUi_.lineFirst().clear();
    annUi_.storedRanges().clear();
    annUi_.clearWordIndexCache();
  } else if (wordIndexCacheHit) {
    if (annUi_.isActive()) {
      annUi_.storedRanges().clear();
      annUi_.clampSelectionToValidWords();
    } else if (!annUi_.annotations().records().empty()) {
      annUi_.updateStoredRangesForPage(*this);
    } else {
      annUi_.storedRanges().clear();
    }
  } else {
    buildPageWordIndex(*page, renderer, fontId, headerFontId, orientedMarginLeft, orientedMarginTop, annUi_.words(),
                       &annUi_.lineFirst(), omitStoredWordStrings);
    if (section != nullptr) {
      annUi_.setWordIndexCache(currentSpineIndex, section->currentPage, fontId, headerFontId, orientedMarginLeft,
                               orientedMarginTop);
    }
    if (annUi_.isActive()) {
      annUi_.storedRanges().clear();
      annUi_.clampSelectionToValidWords();
    } else if (!annUi_.annotations().records().empty()) {
      annUi_.updateStoredRangesForPage(*this);
    } else {
      annUi_.storedRanges().clear();
    }
  }

  const bool textAa = READER_SETTINGS.textAntiAliasing != 0 && renderer.text.supportsAntiAliasing(fontId);

  // Medium is the explicit grayscale mode: run its grayscale refresh for image pages. High stays selective so
  // line-art/comic images can use the sharper 2-bit quantizer without paying for the quality grayscale pass. If
  // text AA already needs a medium grayscale pass, include those non-quality High images in the same pass.
  const bool readerImageTwoBit = READER_SETTINGS.readerImageGrayscale != 0 && pageHasImages;
  const bool highImageMode = READER_SETTINGS.readerImageGrayscale == SystemSetting::READER_IMAGE_HIGH && pageHasImages;
  const bool highQuality = highImageMode && page->anyImageNeedsGrayscale();
  const bool mediumImageGrayscale =
      (READER_SETTINGS.readerImageGrayscale == SystemSetting::READER_IMAGE_MEDIUM && page->hasNonPngImages()) ||
      (highImageMode && !highQuality && textAa);
  const bool needsImageGrayscale = mediumImageGrayscale || highQuality;
  const ImageRenderMode imageMode = readerImageTwoBit ? ImageRenderMode::TwoBit : ImageRenderMode::OneBit;
  const bool pageHasLargeImage =
      pageHasImages && pageImageFootprintAtLeastHalfScreen(*page, renderer, orientedMarginLeft, orientedMarginTop);

  const bool imagePageWithAA = pageHasImages && textAa;

  const bool needsTextAntiAliasPass = textAa;

  // The cleanup refresh belongs to the PREVIOUS page. Gating this on
  // highImageMode (the current page) skips the image -> text transition because
  // the text page has no image, which leaves the previous high-quality image
  // resident on the panel. Use the book setting plus the previous-page marker.
  const bool smartImageRefreshEnabled =
      READER_SETTINGS.readerImageGrayscale == SystemSetting::READER_IMAGE_HIGH && !isBookmarking && !annUi_.isActive();
  const bool smartRefreshAfterQualityImage = lastPageHadHighQualityImage;
  const bool smartRefreshAfterLargeImage = lastPageHadImages && lastPageHadLargeImage;
  const int periodicRefreshPages = READER_SETTINGS.getRefreshFrequency();
  const bool periodicRefreshEnabled = periodicRefreshPages > 0;

  // Sticky has enough PSRAM for chapter-time image display-cache warming, so render the complete page in the
  // first framebuffer pass. Only high-quality grayscale images stay out of this BW base pass because they are
  // intentionally sent through the dedicated quality LUT below.
  const bool skipImagesInPageRender = needsImageGrayscale && highQuality;
  if (skipImagesInPageRender) {
    // The quality pass restores this base buffer before replacing the page's grayscale image rectangles. A
    // normal page turn does not clear the framebuffer, so without this clear an image from the previous page
    // can remain in the saved base wherever the new page has no image to overwrite it.
    renderer.clearScreen(0xFF);
  }
  page->render(renderer, fontId, headerFontId, orientedMarginLeft, orientedMarginTop, skipImagesInPageRender, imageMode,
               /*skipOnlyGrayscaleImages=*/highQuality);

  // Overlay before storeBwBuffer so guide lines are preserved through AA/grayscale passes.
  ReadingGuideLines::render(renderer, *page, bookSettings.readingGuideLinesEnabled, orientedMarginTop,
                            orientedMarginRight, orientedMarginBottom, orientedMarginLeft, fontId);

  renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
  if (isCurrentPageBookmarked()) {
    drawBookmarkIndicator();
  }
  if (currentPageNote()) {
    drawPageNoteIndicator();
  }

  // Medium uses the same BW restore/rebase lifecycle as text AA. Without a
  // snapshot when AA is disabled, the non-preserving grayscale cleanup leaves
  // visible differential ghosting on the following page.
  const bool bwStored = (skipImagesInPageRender || mediumImageGrayscale || (needsTextAntiAliasPass && !highQuality)) &&
                        renderer.storeBwBuffer();
  const bool displayWithQualityPass = highQuality && bwStored;
  const bool smartRefreshThisPageAfterLargeImage = smartImageRefreshEnabled && smartRefreshAfterLargeImage;
  // Async: any grayscale/AA pass that follows already blocks on the refresh finishing before it touches
  // the panel again (every StickyDisplay call starts with finish()), so this only stops blocking the
  // plain-text turn (no further passes needed) on the ~600ms e-ink refresh.
  auto displayPageBuffer = [this, smartRefreshThisPageAfterLargeImage, smartRefreshAfterQualityImage,
                            smartImageRefreshEnabled, periodicRefreshEnabled, periodicRefreshPages]() {
    // A high-quality image ALWAYS gets cleaned up on the next page. It leaves charge no
    // differential turn can neutralise, so it does not matter what the grayscale setting is,
    // or whether bookmarking or the annotation UI happens to be up - all smartImageRefreshEnabled
    // gated. Only the large-image and periodic cases stay conditional.
    if (smartRefreshThisPageAfterLargeImage || smartRefreshAfterQualityImage ||
        (periodicRefreshEnabled && pagesUntilFullRefresh <= 1)) {
      // Same rule as the prepared-page path: after a quality image X4 Pro needs a FULL refresh,
      // since neither Half nor Fast clears what the quality pass leaves. This fallback path is
      // the one a turn takes when the prepared page was invalidated - which is common straight
      // after an image page, so it must not be left on Half.
      const bool afterQualityImage = smartRefreshAfterQualityImage;
      renderer.displayBufferAsync((FREEINK_DEVICE_X4PRO && afterQualityImage)
                                      ? HalDisplay::FULL_REFRESH
                                      : HalDisplay::HALF_REFRESH);
      pagesUntilFullRefresh = periodicRefreshPages;
    } else {
      renderer.displayBufferAsync();
      if (periodicRefreshEnabled) {
        pagesUntilFullRefresh--;
      }
    }
  };

  const bool highQualityCacheReady =
      displayWithQualityPass &&
      page->allGrayscaleImagesCachedTwoBit(renderer, orientedMarginLeft, orientedMarginTop, /*quality=*/true);
  const bool displayImagePlaceholder = displayWithQualityPass && !highQualityCacheReady;
  if (displayImagePlaceholder) {
    page->fillImageRects(renderer, orientedMarginLeft, orientedMarginTop, true, /*onlyGrayscale=*/true);
  }
  // The quality waveform needs the BW page already on the panel as its base. This is especially important on
  // the first render after entering the book: there is no previous page base for the grayscale pass to reuse.
  displayPageBuffer();

  if (highQuality && bwStored) {
    ImageRender::displayGrayscale(
        renderer, /*quality=*/true, /*preserveText=*/true,
        [&] {
          renderer.copyStoredBwToFramebuffer();
          renderer.invertScreen();
          page->fillImageRects(renderer, orientedMarginLeft, orientedMarginTop, false, /*onlyGrayscale=*/true);
          page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop, imageMode, /*quality=*/true,
                             /*onlyGrayscale=*/true);
        },
        kReaderHighQualityFastLut);
#if FREEINK_DEVICE_X4PRO
    constexpr bool kAaAfterHqImage = false;  // see the note above
#else
    constexpr bool kAaAfterHqImage = true;
#endif
    if (needsTextAntiAliasPass && kAaAfterHqImage) {
      const bool textBwStored = renderer.storeBwBuffer();
      if (textBwStored) {
        renderer.renderGrayscalePasses(/*quality=*/false, /*preserveText=*/true, [&] {
          renderer.clearScreen(0x00);
          page->render(renderer, fontId, headerFontId, orientedMarginLeft, orientedMarginTop, /*skipImages=*/true,
                       ImageRenderMode::OneBit);
        });
      }
    }
  } else if (needsTextAntiAliasPass && bwStored && !mediumImageGrayscale) {
    renderer.renderGrayscalePasses(/*quality=*/false, /*preserveText=*/true, [&] {
      renderer.clearScreen(0x00);
      // Regular images are no longer deferred, so keep them in the AA planes instead of allowing the
      // text-only pass to replace their freshly rendered region with the grayscale background.
      page->render(renderer, fontId, headerFontId, orientedMarginLeft, orientedMarginTop, /*skipImages=*/false,
                   ImageRenderMode::OneBit);
    });
  } else if (mediumImageGrayscale || (needsTextAntiAliasPass && bwStored)) {
    ImageRender::displayGrayscale(renderer, /*quality=*/false, /*preserveText=*/bwStored, [&] {
      renderer.clearScreen(0x00);
      if (needsTextAntiAliasPass && bwStored) {
        page->render(renderer, fontId, headerFontId, orientedMarginLeft, orientedMarginTop, /*skipImages=*/true,
                     ImageRenderMode::OneBit);
      }
      if (mediumImageGrayscale) {
        page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop, imageMode);
      }
    });

  } else if (bwStored) {
    renderer.restoreBwBuffer();
  }

  lastPageHadImages = pageHasImages;
  lastPageHadLargeImage = pageHasLargeImage;
  // "Did this page render a high-quality image?" - nothing else. It used to be
  // (highQuality && bwStored), which conflated it with whether a B/W snapshot was taken:
  // bwStored needs skipImagesInPageRender || mediumImageGrayscale || (textAA && !highQuality),
  // and that last term is false on a quality page by construction. So a plain HQ image page
  // left this false and the next page never got its cleanup refresh.
  lastPageHadHighQualityImage = highQuality;

  if (annUi_.isActive()) {
    annUi_.drawUiOverlay(*this);
  } else if (dictUi_.isActive()) {
    dictUi_.drawUiOverlay(*this);
  } else if (footnoteBody_.isActive()) {
    footnoteBody_.drawUiOverlay(*this);
  } else {
    if (highQuality && bwStored) {
      renderer.clearScreen();
      page->render(renderer, fontId, headerFontId, orientedMarginLeft, orientedMarginTop, /*skipImages=*/true,
                   ImageRenderMode::OneBit, /*skipOnlyGrayscaleImages=*/true);
      ReadingGuideLines::render(renderer, *page, bookSettings.readingGuideLinesEnabled, orientedMarginTop,
                                orientedMarginRight, orientedMarginBottom, orientedMarginLeft, fontId);
      renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
      if (isCurrentPageBookmarked()) {
        drawBookmarkIndicator();
      }
      if (currentPageNote()) {
        drawPageNoteIndicator();
      }

      // Remove the quality image from the normal BW framebuffer before it is used to
      // reseed the panel's differential buffer. `false` is paper/white; using `true`
      // here seeds the next page with a black image rectangle that survives page turns.
      page->fillImageRects(renderer, orientedMarginLeft, orientedMarginTop, false, /*onlyGrayscale=*/true);
      renderer.cleanupGrayscaleWithFrameBuffer();
    }

    if (!annUi_.storedRanges().empty()) {
      // Highlights must be the last composition step after a font/size repagination. Rebase from
      // the page now on the panel so this fast update never restores pixels from an old page.
      renderer.syncWriteBufferFromActive();
      annUi_.drawStoredOverlay(*this);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    }
  }
}

/**
 * @brief Renders the status bar with configurable sections
 * @param orientedMarginRight Right margin
 * @param orientedMarginBottom Bottom margin
 * @param orientedMarginLeft Left margin
 */
void EpubActivity::renderStatusBar(const int orientedMarginRight, const int orientedMarginBottom,
                                   const int orientedMarginLeft) const {
  if (statusBar && section) {
    statusBar->render(section.get(), currentSpineIndex, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
  }
}

/**
 * @brief Displays the book title on screen when cover is not available
 */
void EpubActivity::displayBookTitle() {
  renderer.clearScreen();

  std::string bookTitle = epub->getTitle();

  int maxWidth = renderer.getScreenWidth() * 0.6;

  int titleWidth = renderer.text.getWidth(MONTSERRAT_12_FONT_ID, bookTitle.c_str());

  if (titleWidth > maxWidth) {
    bookTitle = renderer.text.truncate(MONTSERRAT_12_FONT_ID, bookTitle.c_str(), maxWidth);
  }

  renderer.text.centered(MONTSERRAT_12_FONT_ID, renderer.getScreenHeight() / 2, bookTitle.c_str(), true,
                         EpdFontFamily::BOLD);
  renderer.displayBuffer();
}

/**
 * @brief Adds a bookmark at the current position
 */
void EpubActivity::addBookmark() {
  if (!epub || !section) return;
  isBookmarking = true;

  const auto result = bookmarks_.toggle(static_cast<uint16_t>(currentSpineIndex),
                                        static_cast<uint16_t>(section->currentPage),
                                        static_cast<uint16_t>(section->pageCount), getCurrentChapterTitle(),
                                        static_cast<uint32_t>(time(nullptr)));
  if (result == EpubBookmarks::ToggleResult::Full) {
    readerPopup("Maximum bookmarks reached");
    return;
  }
  if (result == EpubBookmarks::ToggleResult::Unchanged) return;

  bookmarks_.save(*epub);
  showBookmarkIndicator = result == EpubBookmarks::ToggleResult::Added;
  updateRequired = true;
}

/**
 * @brief Removes a bookmark at the specified index
 * @param index Index of the bookmark to remove
 */
void EpubActivity::removeBookmark(int index) {
  if (epub && index >= 0) {
    bookmarks_.remove(*epub, static_cast<size_t>(index));
  }
}

/**
 * @brief Checks if the current page is bookmarked
 * @return true if bookmarked, false otherwise
 */
bool EpubActivity::isCurrentPageBookmarked() const {
  if (!section) return false;
  return bookmarks_.contains(static_cast<uint16_t>(currentSpineIndex), static_cast<uint16_t>(section->currentPage));
}

/**
 * @brief Navigates to a bookmarked position
 * @param index Index of the bookmark to navigate to
 */
void EpubActivity::goToBookmark(int index) {
  if (index >= 0) {
    const EpubBookmark* bookmark = bookmarks_.at(static_cast<size_t>(index));
    if (!bookmark) return;

    if (currentSpineIndex != bookmark->spineIndex) {
      currentSpineIndex = bookmark->spineIndex;
      nextPageNumber = bookmark->pageNumber;
          section.reset();
    } else if (section) {
      section->currentPage = bookmark->pageNumber;
    }

    updateRequired = true;
  }
}

/**
 * @brief Gets the title of the current chapter
 * @return Chapter title string
 */
std::string EpubActivity::getCurrentChapterTitle() const {
  int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (tocIndex != -1) {
    return epub->getTocItem(tocIndex).title;
  }
  return "Chapter " + std::to_string(currentSpineIndex + 1);
}

/**
 * @brief Draws a bookmark indicator on the current page
 */
void EpubActivity::drawBookmarkIndicator() {
  const int bookmarkWidth = 15;
  const int bookmarkHeight = 25;
  const int bookmarkX = renderer.getScreenWidth() - bookmarkWidth - 15;
  const int bookmarkY = 15;
  const int notchDepth = bookmarkHeight / 4;
  const int centerX = bookmarkX + bookmarkWidth / 2;

  const int xPoints[5] = {bookmarkX, bookmarkX + bookmarkWidth, bookmarkX + bookmarkWidth, centerX, bookmarkX};
  const int yPoints[5] = {bookmarkY, bookmarkY, bookmarkY + bookmarkHeight, bookmarkY + bookmarkHeight - notchDepth,
                          bookmarkY + bookmarkHeight};

  renderer.polygon.render(xPoints, yPoints, 5, true, true);
}

/**
 * @brief Loads book settings from file
 */
void EpubActivity::loadBookSettings() {
  if (epub) {
    FontManager::scanSDFonts("/fonts");
    bool loaded = bookSettings.loadFromFile(epub->getCachePath());
    if (!loaded || bookSettings.readerPresetIndex == 0) {
      READER_PRESETS.applyToBook(0, bookSettings);
    } else {
      syncSettingsFromGlobalIfNeeded();
    }
    renderer.setDarkMode(bookSettings.darkMode != 0);
    pagesUntilFullRefresh = READER_SETTINGS.getRefreshFrequency();
  }
}

/**
 * @brief Saves book settings to file
 */
void EpubActivity::saveBookSettings() {
  std::string cachePath = epub->getCachePath();
  if (cachePath.empty()) {
    return;
  }

  // A book that still inherits the global Reader settings must not get a local
  // settings file. Writing one here would make the next open treat the book as
  // custom and would prevent later global Reader changes from reaching it.
  const std::string settingsPath = cachePath + "/settings.bin";
  if (!bookSettings.useCustomSettings) {
    if (SdMan.exists(settingsPath.c_str())) {
      SdMan.remove(settingsPath.c_str());
    }
    return;
  }

  bookSettings.saveToFile(cachePath);
}

/**
 * @brief Applies current book settings and rebuilds affected sections
 */
void EpubActivity::applyBookSettings() {
  int currentPage = 0;
  int currentSpine = currentSpineIndex;
  const BookSettings rollbackSettings = hasSettingsDrawerSnapshot_ ? settingsDrawerSnapshot_ : bookSettings;

  if (section) {
    currentPage = section->currentPage;
    cachedChapterTotalPageCount = section->pageCount;
    cachedSpineIndex = currentSpine;
  } else {
    currentPage = nextPageNumber;
    cachedChapterTotalPageCount = 0;
  }

  if (!epub) {
    return;
  }

  syncSettingsFromGlobalIfNeeded();
  renderer.setDarkMode(bookSettings.darkMode != 0);
  setupOrientation();

  bookSettings.normalize();
  const int targetFontId = bookSettings.getReaderFontId();
  if (!FontManager::ensureReaderLayoutFonts(targetFontId, renderer)) {
    bookSettings = rollbackSettings;
    bookSettings.normalize();
    renderer.setDarkMode(bookSettings.darkMode != 0);
    setupOrientation();
    bookLayoutAppliedOrientation_ = bookSettings.orientation;
    saveBookSettings();
    hasSettingsDrawerSnapshot_ = false;
    readerPopup("Font load failed");
    updateRequired = true;
    return;
  }
  ViewportInfo info = calculateViewport();

  const int totalSpineItems = epub->getSpineItemsCount();
  if (totalSpineItems <= 0 || currentSpine < 0 || currentSpine >= totalSpineItems) {
    return;
  }

  bool layoutBuildOk = true;
  auto layout = loadingProgressShow("Updating layout", 20);
  vTaskDelay(pdMS_TO_TICKS(50));
  if (!buildSection(currentSpine, info, false, true)) {
    layoutBuildOk = false;
  } else {
    ScreenComponents::LoadingProgress::setProgress(renderer, layout, 100);
  }

  if (!layoutBuildOk) {
    bookSettings = rollbackSettings;
    bookSettings.normalize();
    renderer.setDarkMode(bookSettings.darkMode != 0);
    setupOrientation();
    const int rollbackFontId = bookSettings.getReaderFontId();
    (void)FontManager::ensureReaderLayoutFonts(rollbackFontId, renderer);
    ViewportInfo rollbackInfo = calculateViewport();
    buildSection(currentSpine, rollbackInfo, false, true);

    currentSpineIndex = currentSpine;
    nextPageNumber = currentPage;
    section.reset();

    bookLayoutAppliedOrientation_ = bookSettings.orientation;
    statusBarLayout_.markApplied(bookSettings);
    suppressNextSectionLoadProgress_ = true;
    hasSettingsDrawerSnapshot_ = false;
    saveBookSettings();
    readerPopup("Error updating layout");
    updateRequired = true;
    return;
  }

  currentSpineIndex = currentSpine;
  nextPageNumber = currentPage;

  section.reset();

  bookLayoutAppliedOrientation_ = bookSettings.orientation;
  statusBarLayout_.markApplied(bookSettings);
  suppressNextSectionLoadProgress_ = true;
  hasSettingsDrawerSnapshot_ = false;
  updateRequired = true;
}

/**
 * @brief Initializes reading statistics
 */
void EpubActivity::initStats() {
  if (epub) {
    readingStats_.init(*epub, section.get(), currentSpineIndex);
  }
}

void EpubActivity::maybeCommitReadingSessionCount() {
  if (epub) {
    readingStats_.maybeCommitSession(*epub);
  }
}

void EpubActivity::startPageTimer() { readingStats_.startPageTimer(); }

void EpubActivity::pauseReadingStats() {
  if (epub) {
    readingStats_.pausePageTimer(*epub, section.get(), currentSpineIndex);
  }
}

void EpubActivity::endPageTimer() {
  if (epub) {
    readingStats_.endPageTimer(*epub, section.get(), currentSpineIndex);
  }
}

void EpubActivity::saveBookStats() {
  if (epub) {
    readingStats_.save(*epub);
  }
}

void EpubActivity::displayBookStats() {
  if (epub) {
    readingStats_.display(renderer, *epub);
  }
}

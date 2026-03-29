/**
 * @file PdfReaderActivity.h
 * @brief Public interface and types for PdfReaderActivity.
 */

/**
 * PdfReaderActivity.h
 *
 * Native reflowable PDF reader activity, architected the same way EPUB reading is: the book's text is
 * extracted and laid out into Page/PageLine/TextBlock objects (the same generic, serializable page-layout
 * types Epub's Section uses - see lib/Epub/Epub/Page.h and blocks/TextBlock.h), and that already-wrapped
 * page data is cached to disk; every later open just deserializes pages directly instead of re-running text
 * layout. Bold/italic styling from the source PDF is preserved and paragraphs whose text is visually larger
 * than the document's body size are rendered as bold headings. This is standalone PDF code - it doesn't go
 * through the Epub or Txt classes/activities, only their shared page-layout data structures.
 *
 * PDFs have no chapter/TOC structure to build incrementally like EPUB's per-spine-item Sections, so the
 * book is instead split into kChunkCount source-page-range "chunks" (each roughly 1/kChunkCount of the
 * book) and each one is parsed/laid out/cached independently - mirroring EPUB's actual mechanism
 * (EpubActivity::loadCurrentSection()/buildSection(), Section's own per-spine-item cache file) rather than
 * its chapter boundaries specifically. Only the chunk the reader is currently showing is ever built
 * on-demand (blocking, with a progress screen); the chunk after it is opportunistically built in the
 * background once idle, matching EpubActivity's runIdleNextSectionBuild(). A page-turn that reaches either
 * end of the current chunk simply switches the active chunk (building it first if it isn't cached yet) -
 * see loop() and ensureChunkLoaded().
 */

#pragma once

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <Pdf.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <vector>

#include "activity/ActivityWithSubactivity.h"
#include "state/SystemSetting.h"
#include "system/ScreenComponents.h"

class PdfReaderActivity final : public ActivityWithSubactivity {
  // How (if at all) to show progress while a chunk is being built. FullScreen is only for the very first
  // chunk of a freshly-opened book (nothing to show underneath yet); Overlay is for every later
  // uncached-chunk build reached by turning a page mid-read, which draws a small popup on top of the page
  // that's already on screen instead of blanking it - the reader was already looking at real content, not a
  // loading screen. None is for the silent background prefetch (see serviceIdleNextChunkBuild()).
  enum class ChunkProgressUi { None, FullScreen, Overlay };
  // Thrown out of maybeYield() to unwind an in-progress *background* chunk build the instant the user does
  // something - the interpreter's recursive parse/layout call chain has no natural "pause point" to make it
  // cooperatively resumable (unlike EPUB's Section::stepIncrementalBuild(), which processes explicit byte
  // ranges of already-parsed HTML), so interruption here means abandoning that attempt outright and retrying
  // on the next idle window rather than resuming mid-page. Never thrown during the foreground/on-demand
  // build the user is actively waiting on - see backgroundBuildActive.
  struct BuildAborted {};

  std::unique_ptr<Pdf> pdf;
  TaskHandle_t displayTaskHandle = nullptr;
  StaticTask_t taskControlBlock{};
  StackType_t* taskStack = nullptr;  // heap_caps_malloc'd from PSRAM - see onEnter()
  SemaphoreHandle_t renderingMutex = nullptr;
  int chunkCount = 1;    // pdf->getPageCount() split into up to kChunkCount source-page-range chunks
  int currentChunk = 0;  // which chunk is currently loaded/active
  int currentPage = 0;   // device page index, local to currentChunk
  int totalPages = 0;    // device page count, local to currentChunk
  bool updateRequired = false;
  const std::function<void()> onGoBack;
  const std::function<void()> onGoToHome;

  FsFile pagesFile;                       // currentChunk's cached, already-laid-out page data
  std::vector<uint32_t> pageFileOffsets;  // byte offset of each page within pagesFile
  std::unique_ptr<Page> currentPageData;
  bool textReady = false;  // true once currentChunk has been loaded/built (see displayTaskLoop)

  int cachedFontId = 0;
  int cachedHeaderFontId = 0;  // same family as cachedFontId, one size step up - see computeLayoutMetrics()
  int cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = SystemSetting::LEFT_ALIGN;
  uint8_t cachedExtraParagraphSpacing = 1;
  float cachedLineCompression = 1.0f;   // READER_SETTINGS.getReaderLineCompression() - the "line height %" setting
  float cachedWordSpacingFactor = 1.0f;  // READER_SETTINGS.getReaderWordSpacingFactor() - "text spacing %" setting
  int viewportWidth = 0;
  int viewportHeight = 0;
  unsigned long lastYieldMs = 0;
  unsigned long lastProgressRedrawMs = 0;
  int imagesCached = 0;  // running count, used to name cached image files uniquely during buildChunkCache()
  bool overlayPopupShown = false;  // whether the current build has already drawn its "Loading pages..." popup
  ScreenComponents::PopupLayout overlayPopupLayout{};  // so later progress ticks can redraw just its bar

  // Idle-prefetch-next-chunk state, mirroring EpubActivity's scheduleIdleNextSectionBuild()/
  // runIdleNextSectionBuild() 1.5s-idle-debounce pattern (EpubActivity.cpp).
  unsigned long idleSinceMs = 0;               // millis() of the last render/input activity
  bool nextChunkPrefetchAttempted = false;      // cleared whenever currentChunk changes - see switchToChunk()
  bool backgroundBuildActive = false;           // true only while buildChunkCache() runs for a *prefetch*,
                                                // gates whether maybeYield() may throw BuildAborted

  // Time-based (not iteration-count-based) watchdog yield: the reader's chosen font can be an SD-streaming
  // font whose glyph metrics are fetched on demand, so a fixed iteration count can't bound how long a stretch
  // of work takes - only wall-clock time can. Safe to call from any hot loop in this file. Also the
  // cancellation point for an in-progress background prefetch - see backgroundBuildActive/BuildAborted.
  void maybeYield();
  // Full-screen progress view for the very first chunk of a freshly-opened book, styled like
  // ThumbnailGeneratorActivity's progress view (eyebrow + title + thin progress bar + count line) so that
  // wait isn't a single static "Preparing book..." popup for however long it takes. Throttled internally to
  // an e-ink-friendly redraw cadence - safe to call as often as progress actually changes.
  void renderBuildProgress(const char* status, int current, int total, bool force = false);
  // Small "Loading pages..." popup drawn on top of whatever's currently on screen, for every later chunk
  // build reached by turning a page - see ChunkProgressUi::Overlay. Draws the popup itself on first call,
  // then just updates its progress bar on later calls (throttled like renderBuildProgress()).
  void renderChunkLoadingOverlay(int current, int total, bool force = false);
  // Dispatches to renderBuildProgress()/renderChunkLoadingOverlay()/nothing depending on `ui` - the single
  // call site buildChunkCache() uses so it doesn't need to know which UI mode it's running under.
  void reportBuildProgress(ChunkProgressUi ui, const char* status, int current, int total, bool force = false);

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void prepareBook();
  void renderScreen();
  void renderPage();

  void computeLayoutMetrics();
  // [startPage, endPage) source pages covered by chunk `chunkIndex`, splitting pdf's page count into
  // chunkCount roughly-equal ranges.
  void chunkPageRange(int chunkIndex, int& startPage, int& endPage) const;
  std::string chunkCachePath(int chunkIndex) const;
  // Header-only check (opens/closes the chunk's cache file without touching any reader state) - safe to
  // call speculatively, e.g. to decide whether a background prefetch is even necessary.
  bool chunkCacheIsValid(int chunkIndex) const;
  // Loads chunk `chunkIndex`'s cache file into pagesFile/pageFileOffsets/totalPages, making it the active
  // chunk for rendering. Returns false if no valid cache exists yet (caller must buildChunkCache() first).
  bool loadChunkIntoActiveState(int chunkIndex);
  void buildChunkCache(int chunkIndex, ChunkProgressUi ui);
  // Makes `chunkIndex` the active chunk, building it first (blocking, with the given progress UI) if it
  // isn't already cached - the on-demand path EpubActivity::loadCurrentSection() mirrors for a chapter miss.
  void switchToChunk(int chunkIndex, int startOnPage, ChunkProgressUi ui);
  // Builds currentChunk+1 in the background once idle, mirroring EpubActivity::runIdleNextSectionBuild() -
  // interruptible via BuildAborted (see maybeYield()), so a page turn during a prefetch attempt just
  // abandons it instead of delaying input.
  void serviceIdleNextChunkBuild();
  void loadCurrentPageData();
  void ensureThumbnailExists();
  void saveProgress() const;
  void loadProgress();

 public:
  explicit PdfReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Pdf> pdf,
                             const std::function<void()>& onGoBack, const std::function<void()>& onGoToHome)
      : ActivityWithSubactivity("PdfReader", renderer, mappedInput),
        pdf(std::move(pdf)),
        onGoBack(onGoBack),
        onGoToHome(onGoToHome) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
};

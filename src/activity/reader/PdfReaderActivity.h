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
  enum class ChunkProgressUi { None, FullScreen, Overlay };
  struct BuildAborted {};

  std::unique_ptr<Pdf> pdf;
  TaskHandle_t displayTaskHandle = nullptr;
  StaticTask_t taskControlBlock{};
  StackType_t* taskStack = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  int chunkCount = 1;
  int currentChunk = 0;
  int currentPage = 0;
  int totalPages = 0;
  bool updateRequired = false;
  const std::function<void()> onGoBack;
  const std::function<void()> onGoToHome;

  FsFile pagesFile;
  std::vector<uint32_t> pageFileOffsets;
  std::unique_ptr<Page> currentPageData;
  bool textReady = false;

  int cachedFontId = 0;
  int cachedHeaderFontId = 0;
  int cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = SystemSetting::LEFT_ALIGN;
  uint8_t cachedExtraParagraphSpacing = 1;
  float cachedLineCompression = 1.0f;
  float cachedWordSpacingFactor = 1.0f;
  int viewportWidth = 0;
  int viewportHeight = 0;
  unsigned long lastYieldMs = 0;
  unsigned long lastProgressRedrawMs = 0;
  int imagesCached = 0;
  bool overlayPopupShown = false;
  ScreenComponents::PopupLayout overlayPopupLayout{};

  unsigned long idleSinceMs = 0;
  bool nextChunkPrefetchAttempted = false;
  bool backgroundBuildActive = false;

  void maybeYield();
  void renderBuildProgress(const char* status, int current, int total, bool force = false);
  void renderChunkLoadingOverlay(int current, int total, bool force = false);
  void reportBuildProgress(ChunkProgressUi ui, const char* status, int current, int total, bool force = false);

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void prepareBook();
  void renderScreen();
  void renderPage();

  void computeLayoutMetrics();
  void chunkPageRange(int chunkIndex, int& startPage, int& endPage) const;
  std::string chunkCachePath(int chunkIndex) const;
  bool chunkCacheIsValid(int chunkIndex) const;
  bool loadChunkIntoActiveState(int chunkIndex);
  void buildChunkCache(int chunkIndex, ChunkProgressUi ui);
  void switchToChunk(int chunkIndex, int startOnPage, ChunkProgressUi ui);
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

#pragma once

/**
 * @file EpubActivity.h
 * @brief Public interface and types for EpubActivity.
 */

#include <Epub.h>
#include <Epub/PageWordIndex.h>
#include <Epub/Section.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "EpubAnnotationUi.h"
#include "EpubBookmark.h"
#include "EpubBookmarks.h"
#include "EpubDictionaryUi.h"
#include "EpubFootnoteBody.h"
#include "EpubImageViewerUi.h"
#include "EpubReadingStats.h"
#include "GoToPercentUi.h"
#include "OrientationPickerUi.h"
#include "PresetPickerUi.h"
#include "QuickActionsMenuUi.h"
#include "ReaderButtonBindings.h"
#include "SettingsDrawer.h"
#include "StatusBar.h"
#include "activity/ActivityWithSubactivity.h"
#include "state/BookProgress.h"
#include "state/BookSetting.h"
#include "system/ScreenComponents.h"

struct ViewportInfo {
  int totalMarginTop;
  int totalMarginBottom;
  int totalMarginLeft;
  int totalMarginRight;
  uint16_t width;
  uint16_t height;
  int fontId;
  float lineCompression;
  float wordSpacing;
};

/**
 * Main activity for reading EPUB books.
 * Handles page navigation, bookmarks, settings, and reading statistics.
 */
class EpubActivity final : public ActivityWithSubactivity {
  friend class EpubAnnotationUi;
  friend class EpubDictionaryUi;
  friend class EpubFootnoteBody;
  friend class EpubImageViewerUi;
  friend class OrientationPickerUi;
  friend class PresetPickerUi;
  friend class QuickActionsMenuUi;
  friend class GoToPercentUi;
  friend class ReaderButtonBindings;
  friend class EpubNavigation;
  friend class WordLookup;

 public:
  /**
   * Constructs a new EpubActivity.
   *
   * @param renderer Reference to the graphics renderer
   * @param mappedInput Reference to the input manager
   * @param epub Unique pointer to the EPUB document
   * @param onGoBack Callback for returning to previous activity
   * @param onGoToHome Callback for navigating to Home
   */
  explicit EpubActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub,
                        const std::function<void()>& onGoBack, const std::function<void()>& onGoToHome,
                        int initialSpineIndex = -1, int initialPageNumber = -1);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  /** Match Crosspoint-style reader: pump display from loop (no separate FreeRTOS render task). */
  bool skipLoopDelay() override { return true; }
  /** Flick/shake page turns never register as button activity, so with auto-sleep on the device would
   *  fall asleep mid-read; suppress the idle timeout in-book while that gesture is enabled. */
  bool preventAutoSleep() override;

 private:
  int currentFontId;
  int nextFontId;
  bool isToggleClosed = false;
  bool isBookmarking = false;
  std::unique_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  std::unique_ptr<BookProgress> bookProgress = nullptr;
  std::unique_ptr<StatusBar> statusBar = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  const int initialSpineIndex_;
  const int initialPageNumber_;
  int pagesUntilFullRefresh = 0;
  bool pendingPercentJump = false;
  float pendingSpineProgress = 0.0f;
  bool suppressNextSectionLoadProgress_ = false;
  bool suppressBackUntilReleased_ = false;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  bool updateRequired = false;
  int loadingProgress = 0;
  unsigned long lastAutoPageTurnTime = 0;

  const std::function<void()> onGoBack;
  const std::function<void()> onGoToHome;

  EpubBookmarks bookmarks_;
  bool showBookmarkIndicator = false;
  bool lastPageHadImages = false;
  /** Previous page: image union bbox at least half screen in both dimensions (for smart HALF refresh). */
  bool lastPageHadLargeImage = false;
  /** Previous page rendered a high-quality grayscale image that needs an absolute cleanup on the next page. */
  bool lastPageHadHighQualityImage = false;

  // The display owns two framebuffers. After a page is shown, the inactive one
  // is writable; use it as a validated, already-composed forward page rather
  // than doing SD/decoder work on the user's page turn.
  struct PreparedPage {
    bool ready = false;
    int spineIndex = -1;
    int pageIndex = -1;
    uint32_t renderSignature = 0;
    uint8_t refreshMode = 0;
    int pagesUntilFullRefresh = 0;
    bool hasImages = false;
    bool hasLargeImage = false;
    bool hasHighQualityImage = false;
    bool highQuality = false;
    bool mediumImageGrayscale = false;
    bool textAntiAlias = false;
    bool imagePlaceholder = false;
  };
  PreparedPage preparedPage_;
  int preparedPageSpineIndex_ = -1;
  int preparedPageIndex_ = -1;
  unsigned long preparePageAfterMs_ = 0;
  int preloadedPageSpineIndex_ = -1;
  int preloadedPageIndex_ = -1;

  int lastGoodSpineIndex_ = 0;

  int lastGoodPageNumber_ = 0;
  bool chapterRecoveryAttempted_ = false;

  SettingsDrawer* settingsDrawer = nullptr;
  bool settingsDrawerVisible = false;
  std::unique_ptr<class EpubNavigation> navigation_;
  BookSettings bookSettings;
  BookSettings settingsDrawerSnapshot_;
  bool hasSettingsDrawerSnapshot_ = false;
  /** Last orientation value used for a full layout/section rebuild; used to detect drift after global sync. */
  uint8_t bookLayoutAppliedOrientation_ = 0xFF;
  /** Tracks the status-bar layout used for the current section pagination. */
  StatusBar::LayoutState statusBarLayout_;

  EpubReadingStats readingStats_;

  /** @param clearFramebuffer When false, skips clearScreen before compositing (same-page annotation overlay refresh).
   */
  void renderScreen(bool clearFramebuffer = true);

  bool handleTopRightTap();
  bool handleImageTouch();
  bool handleWordTouch();
  bool handleWordSelection();
  bool openWordSelection(int x, int y);
  void startVoiceNoteForSelection(const std::string& selectedText, uint16_t wordLo, uint16_t wordHi);
  void startVoiceNoteForPage();
  void closeWordSelection();
  void renderWordSelection();
  int wordAt(int x, int y) const;
  /** Base word-action list ("Look up"/"Highlight"/"Add note") plus "View footnote" appended when the
   * currently selected word (touchWords_[selectedWord_]) is a footnote/link marker. */
  std::vector<std::string> currentWordActions() const;

  const EpubAnnotationRecord* currentPageNote() const;
  bool handlePageNoteTouch();
  void handlePageNotePopupInput();
  void renderPageNotePopup();
  void pollPageNoteTranscription();
  void startPageNoteTranscription();
  void savePageNoteAudio(const std::string& audioPath);
  void savePageNoteText(const std::string& text);
  bool consumePageNoteVoiceCompletion();
  bool handlePageNoteOverlay();
  void resetPageNoteState();

  /**
   * Handles page turning logic for forward/backward navigation.
   * Manages chapter transitions and end-of-book detection.
   *
   * @param forward True for forward page turn, false for backward
   */
  /** @return true when a previously composed forward page was displayed directly. */
  bool pageTurn(bool forward);

  /**
   * Renders page contents with margins and status bar.
   *
   * @param page Page to render
   * @param orientedMarginTop Top margin
   * @param orientedMarginRight Right margin
   * @param orientedMarginBottom Bottom margin
   * @param orientedMarginLeft Left margin
   */
  void renderContents(Page* page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);

  /**
   * Fast, minimal render of a page pulled straight out of an in-progress chapter build
   * (Section::loadIncrementalPage()), shown while the rest of that chapter keeps building in the
   * background. Deliberately skips annotations/AA/grayscale passes - it's a preview, superseded within a
   * tick or two by the normal renderContents() once the chapter finishes and `section` is set.
   */

  /**
   * Renders the status bar with configurable sections.
   *
   * @param orientedMarginRight Right margin
   * @param orientedMarginBottom Bottom margin
   * @param orientedMarginLeft Left margin
   */
  void renderStatusBar(int orientedMarginRight, int orientedMarginBottom, int orientedMarginLeft) const;

  /**
   * Saves current reading progress to file using BookProgress handler.
   *
   * @param spineIndex Current spine index
   * @param currentPage Current page number
   * @param pageCount Total pages in current chapter
   */
  void saveProgress(int spineIndex, int currentPage, int pageCount, bool saveRecentNow = true);

  /**
   * Loads progress from file using BookProgress handler.
   */
  void loadProgress();

  /**
   * Opens the dedicated Table of Contents sidebar.
   */
  void openTableOfContents();

  /**
   * Toggles the settings drawer visibility.
   */
  void toggleSettingsDrawer();

  void jumpToPercent(int percent);

  void displayBookTitle();
  void drawPreparingBookScreen();
  void drawLoadingScreen();

  /** Close drawers (if open), then show a centered popup message. */
  void readerPopup(const char* message);

  /** After a failed chapter load: popup, revert once to last good chapter, then clear cache and exit if still broken.
   */
  void handleChapterLoadFailure();

  /** Close drawers (if open), then show the bottom loading progress panel. */
  ScreenComponents::LoadingProgressLayout loadingProgressShow(const char* message, int progressPercent0to100);

  void addBookmark();
  void removeBookmark(int index);
  bool isCurrentPageBookmarked() const;
  void goToBookmark(int index);
  std::string getCurrentChapterTitle() const;
  void drawBookmarkIndicator();
  void drawPageNoteIndicator();

  EpubAnnotationUi annUi_;
  EpubDictionaryUi dictUi_;
  EpubFootnoteBody footnoteBody_;
  EpubImageViewerUi imageViewerUi_;
  bool wordSelectionOpen_ = false;
  bool wordActionsOpen_ = false;
  int selectedWord_ = -1;
  std::vector<PageWordHit> touchWords_;
  bool pageNotePopupOpen_ = false;
  bool pageNoteTranscriptionPending_ = false;
  uint32_t pageNoteLastRefreshMs_ = 0;
  uint8_t pageNoteDots_ = 1;
  std::string pageNoteCachePath_;
  EpubAnnotationRecord pageNoteRecord_;
  bool pageNoteVoiceCompletionPending_ = false;
  bool pageNoteVoiceSuccess_ = false;
  std::string pageNoteVoicePath_;
  OrientationPickerUi orientationPicker_;
  PresetPickerUi presetPicker_;
  QuickActionsMenuUi quickActionsUi_;
  GoToPercentUi goToPercentUi_;
  ReaderButtonBindings btnBindings_;

  /**
   * Applies current book settings and rebuilds affected sections.
   */
  void applyBookSettings();

  void saveBookSettings();
  void loadBookSettings();

  void initStats();
  void maybeCommitReadingSessionCount();
  void startPageTimer();
  void pauseReadingStats();
  void endPageTimer();
  void saveBookStats();

  /**
   * Calculates the viewport dimensions based on current settings.
   *
   * @return ViewportInfo structure containing viewport dimensions and settings
   */
  ViewportInfo calculateViewport();

  /**
   * Builds a section file for a given spine index.
   *
   * @param spineIndex Index of the spine to build
   * @param info Viewport information for rendering
   * @param showProgress Whether to show progress during building
   * @param skipImages If true, skip processing new images and only use existing cached images
   * @return true if successful, false otherwise
   */
  bool buildSection(int spineIndex, const ViewportInfo& info, bool showProgress = false, bool skipImages = false);

  /**
   * Loads a section for a given spine index.
   *
   * @param spineIndex Index of the spine to load
   * @param info Viewport information for rendering
   * @return Unique pointer to the loaded section
   */
  std::unique_ptr<Section> loadSection(int spineIndex, const ViewportInfo& info, bool showProgress = true);

  void setupOrientation();
  /** Refreshes inherited reader defaults into books that do not use custom settings. */
  bool syncSettingsFromGlobalIfNeeded();
  /** Settings drawer callback: keep renderer, drawer, and menu layout in sync while editing. */
  void onBookSettingsLiveLayoutSync();
  /** @param coverAvailable Whether displayCoverOrTitle() already confirmed a real cover exists (cached or
   *  freshly extracted) - false skips retrying a thumbnail extraction from the same cover entry that just
   *  failed (or was never there), while the packaged META-INF/thumbnail.jpg path is still tried either way. */
  void ensureThumbnailExists(bool coverAvailable);
  /** @return true if a real cover image (not just the title fallback) is now cached on disk. */
  bool displayCoverOrTitle();
  void loadCurrentSection(bool showProgress = true);
  void scheduleIdlePreparedPage();
  void runIdlePreparedPage();
  void runIdleSecondNextPage();
  bool composePreparedForwardPage();
  bool presentPreparedForwardPage();
  bool finishPreparedPageGrayscale(const PreparedPage& prepared);
  bool canPrepareForwardPage() const;
  void invalidatePreparedPage();
  uint32_t preparedPageRenderSignature();
  void updateExternalState();
  void fastPath();
  bool slowPath();
  void displayBookStats();
};

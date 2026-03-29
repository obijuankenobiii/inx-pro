#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "EpubAnnotations.h"
#include "WordLookup.h"

class EpubActivity;

/**
 * Highlight UI: chord entry, D-pad navigation, framebuffer capture/repaint, overlays, and persistence via
 * EpubAnnotations.
 */
class EpubAnnotationUi {
 public:
  EpubAnnotationUi();

  bool isActive() const { return mode_; }

  EpubAnnotations& annotations() { return annotations_; }
  const EpubAnnotations& annotations() const { return annotations_; }

  std::vector<PageWordHit>& words() { return wordLookup_.words(); }
  const std::vector<PageWordHit>& words() const { return wordLookup_.words(); }

  std::vector<size_t>& lineFirst() { return wordLookup_.lineFirst(); }
  const std::vector<size_t>& lineFirst() const { return wordLookup_.lineFirst(); }

  std::vector<std::pair<size_t, size_t>>& storedRanges() { return storedRanges_; }
  const std::vector<std::pair<size_t, size_t>>& storedRanges() const { return storedRanges_; }

  int wordIndexCacheSpine() const { return wordIndexCacheSpine_; }
  int wordIndexCachePage() const { return wordIndexCachePage_; }
  int wordIndexCacheFontId() const { return wordIndexCacheFontId_; }
  int wordIndexCacheHeaderFontId() const { return wordIndexCacheHeaderFontId_; }
  int wordIndexCacheMarginL() const { return wordIndexCacheMarginL_; }
  int wordIndexCacheMarginT() const { return wordIndexCacheMarginT_; }

  void setWordIndexCache(int spine, int page, int fontId, int headerFontId, int marginL, int marginT);

  void clearWordIndexCache();

  void clearSessionAndCapture();

  void tryChordEnter(EpubActivity& act);
  void enter(EpubActivity& act);
  bool startAt(EpubActivity& act, int x, int y);
  void exit(EpubActivity& act);
  void handleInput(EpubActivity& act);
  void repaint(EpubActivity& act);

  void ensureDiskListLoaded(EpubActivity& act);
  void updateStoredRangesForPage(const EpubActivity& act);

  void drawStoredOverlay(EpubActivity& act);
  void drawUiOverlay(EpubActivity& act);

  void setPendingNoteAudioPath(std::string path) { pendingNoteAudioPath_ = std::move(path); }
  bool hasPendingNoteAudio() const { return !pendingNoteAudioPath_.empty(); }
  void setPendingNoteText(std::string text) { pendingNoteText_ = std::move(text); }

  std::string extractRangeText(size_t anchorFlat, size_t focusFlat) const;
  void saveToStorage(EpubActivity& act);

  void prepareWordGeometry(EpubActivity& act);

  /** After layout/cache updates, keep focus/anchor within word list. */
  void clampSelectionToValidWords();

 private:
  void drawLatticeHighlightRect(EpubActivity& act, int x, int y, int width, int height);
  void drawLatticeHighlightForWordIndexRange(EpubActivity& act, size_t lo, size_t hi);
  void drawHighlights(EpubActivity& act);

  bool focusAt(int x, int y);
  bool tryNavigationHoldRepeat(EpubActivity& act);
  bool captureFramebuffer(EpubActivity& act);

  bool hasSaveableContent() const;
  void resetSelectionToStart(EpubActivity& act);
  void clearAllStoredHighlightsOnCurrentPage(EpubActivity& act);
  /** Sort by lo; merge overlapping or adjacent word spans. */
  static void normalizeSpans(std::vector<std::pair<size_t, size_t>>& spans);

  bool mode_ = false;
  bool controlsVisible_ = true;
  /** While true, drawUiOverlay() is a no-op - lets a renderScreen() call rebuild the page/word-index
   *  cache without baking the cursor box into the framebuffer that gets captured right
   *  after (clearAllStoredHighlightsOnCurrentPage() recapture). */
  bool suppressOverlayDraw_ = false;
  WordLookup wordLookup_;
  // Compatibility aliases keep annotation-specific range code compact while
  // WordLookup owns the actual geometry, cursor, navigation and snapshot.
  std::vector<PageWordHit>& words_ = wordLookup_.words();
  std::vector<size_t>& lineFirst_ = wordLookup_.lineFirst();
  size_t& focus_ = wordLookup_.focus();
  size_t anchor_ = 0;

  unsigned long chordStartMs_ = 0;
  bool chordConsumed_ = false;
  bool selectingStarted_ = false;
  /** Completed ranges while browsing between Start/Stop cycles (same page). */
  std::vector<std::pair<size_t, size_t>> pendingSpans_;
  EpubAnnotations annotations_;
  std::vector<std::pair<size_t, size_t>> storedRanges_;
  std::string pendingNoteAudioPath_;
  std::string pendingNoteText_;

  int wordIndexCacheSpine_ = -1;
  int wordIndexCachePage_ = -1;
  int wordIndexCacheFontId_ = -1;
  int wordIndexCacheHeaderFontId_ = -1;
  int wordIndexCacheMarginL_ = INT_MIN;
  int wordIndexCacheMarginT_ = INT_MIN;
};

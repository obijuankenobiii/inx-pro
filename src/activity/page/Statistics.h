#pragma once

/**
 * @file Statistics.h
 * @brief Public interface and types for Statistics.
 */

#include <string>
#include <utility>
#include <vector>
#include <functional>

#include "SubPage.h"
#include "state/Statistics.h"

class Bitmap;

/**
 * Activity for displaying reading statistics.
 * First view is a global reading-stats summary; Up/Down steps through one book at a time.
 * On enter, global totals load from `/.system/statistics.bin` and saved per-book stat records are indexed.
 * Confirm (Refresh) rescans per-book stats, recomputes aggregates, and writes that snapshot.
 */
class Statistics final : public SubPage {
 private:
  /** 0 = aggregated global overview; 1..N = one book (index N-1 in allBooksStats). */
  int viewIndex = 0;

  std::vector<BookReadingStats> allBooksStats;
  std::vector<uint8_t> loadedBookStatsFlags_;
  GlobalReadingStats globalStats;

  /**
   * Loads and sorts reading statistics for all books by most recently read.
   * Refreshes aggregates from disk, updates the global snapshot file, and redraws progress UI.
   */
  void loadStats();

  /** Loads saved global totals and indexes persisted per-book stats without loading every record. */
  void hydrateFromStorage();
  void indexBookStatsPaths(bool includeMetadata = true);
  bool ensureBookStatsLoaded(int bookIdx);

  /**
   * Renders a book cover or placeholder at the specified position.
   */
  void renderCover(const std::string& bookPath, int x, int y, int width, int height, const std::string& title,
                   const std::string& author) const;

  /** Global view: draw recent cover with frame sized to bitmap (max 165×182); returns {frameW, frameH}. */
  std::pair<int, int> drawGlobalRecentThumbBlock(int x, int y, const std::string& bookPath,
                                                 const std::string& title) const;

  void content() override;

  void renderSingleBookView(int bookIdx, int contentTop, int contentBottom) const;

  /** Global overview (view 0): each returns next Y after band height + Margin. */
  int renderRecent(int y, int innerLeft, int innerRight, int innerW, int Margin) const;
  int renderFirstGrid(int y, int innerLeft, int innerW, int Margin) const;
  int renderGuage(int y, int innerLeft, int innerRight, int Margin) const;
  void renderSecondGrid(int y, int innerLeft, int innerRight, int contentBottom) const;

 public:
  Statistics(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> close);

  const char* name() const override { return "Reading Stats"; }

  void onEnter() override;
  void onExit() override;
  void loop() override;
};

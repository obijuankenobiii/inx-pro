#pragma once

/**
 * @file Statistics.h
 * @brief Public interface and types for Statistics.
 */

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <functional>

#include "SubPage.h"
#include "state/Statistics.h"
#include "util/LibraryIndex.h"

class Bitmap;

/**
 * Activity for displaying reading statistics.
 * The main view shows the global reading-stats summary; Up/Down steps through one book at a time.
 * The Week subpage shows the persisted reading history with cached book thumbnails.
 * On enter, global totals load from `/.system/statistics.bin` and saved per-book stat records are indexed.
 * Confirm (Refresh) rescans per-book stats, recomputes aggregates, and writes that snapshot.
 */
class Statistics final : public SubPage {
 private:
  enum class ChartPeriod : uint8_t {
    Week,
    Month,
    Year,
  };

  /** 0 = aggregated global overview; 1..N = one book (index N-1 in allBooksStats). */
  int viewIndex = 0;
  int statsTab_ = 0;
  int selectedBookIndex_ = -1;
  int booksPage_ = 0;

  std::vector<BookReadingStats> allBooksStats;
  std::vector<uint8_t> loadedBookStatsFlags_;
  std::vector<BookListMetadata> bookListMetadata_;
  std::vector<LibraryIndex::Book> indexedBooks_;
  GlobalReadingStats globalStats;
  int64_t calendarWeekStartDay_ = 0;
  std::array<uint32_t, 7> weeklyReadingTimeMs_{};
  uint32_t weeklyTotalReadingTimeMs_ = 0;
  uint32_t weeklyGoalTimeMs_ = 0;
  int64_t weeklyChartWeekStartDay_ = 0;
  ChartPeriod chartPeriod_ = ChartPeriod::Week;
  int64_t chartAnchorDay_ = 0;
  int64_t chartPeriodStartDay_ = 0;
  std::array<uint32_t, 31> monthlyReadingTimeMs_{};
  uint32_t monthlyTotalReadingTimeMs_ = 0;
  uint32_t monthlyGoalTimeMs_ = 0;
  int monthlyChartDays_ = 0;
  int monthlyChartOffset_ = 0;
  std::array<uint32_t, 12> yearlyReadingTimeMs_{};
  uint32_t yearlyTotalReadingTimeMs_ = 0;
  uint32_t yearlyGoalTimeMs_ = 0;

  /**
   * Loads and sorts reading statistics for all books by most recently read.
   * Refreshes aggregates from disk, updates the global snapshot file, and redraws progress UI.
   */
  void loadStats(bool showProgress = true);

  /** Loads the saved global snapshot and the persisted, already-built book index. */
  void hydrateFromStorage();
  void loadIndexedBooks();
  bool ensureBookStatsLoaded(int bookIdx);
  void refreshWeeklyChartData();
  void refreshMonthlyChartData();
  void refreshYearlyChartData();
  void refreshChartData();
  void moveChartPeriod(int direction);
  bool scrollMonthlyChart(int direction);

  /**
   * Renders a book cover or placeholder at the specified position.
   */
  void renderCover(const std::string& bookPath, int x, int y, int width, int height, const std::string& title,
                   const std::string& author) const;

  /** Global view: draw recent cover with frame sized to bitmap (max 165×182); returns {frameW, frameH}. */
  std::pair<int, int> drawGlobalRecentThumbBlock(int x, int y, const std::string& bookPath,
                                                 const std::string& title) const;

  void content() override;
  void menu() override;
  void title() const override;
  void renderWeeklyChart(int chartTop, int chartBottom) const;
  void renderMonthlyChart(int chartTop, int chartBottom) const;
  void renderYearlyChart(int chartTop, int chartBottom) const;
  void renderRadialChart(int chartTop, int chartBottom, int chartLeft, int chartRight) const;
  void renderBooksView(int contentTop, int contentBottom);
  void renderBookListThumbnail(const BookListMetadata& book, int x, int y, int width, int height) const;

  void renderSingleBookView(int bookIdx, int contentTop, int contentBottom) const;
  void renderSingleBookTopSection(int bookIdx) const;
  void renderSingleBookMiddleChart(int bookIdx) const;
  void renderSingleBookBottomGrid(int bookIdx) const;

  /** Legacy recent-book layout helper retained for cached cover sizing. */
  int renderRecent(int y, int innerLeft, int innerRight, int innerW, int Margin) const;
  int renderCalendar(int y, int innerLeft, int innerW, int contentBottom) const;
  int renderFirstGrid(int y, int innerLeft, int innerW, int Margin) const;
  int renderGuage(int y, int innerLeft, int innerRight, int Margin) const;
  void renderSecondGrid(int y, int innerLeft, int innerRight, int contentBottom) const;
  bool handleControlDrawerInput();
  bool handleHamburgerTap();
  bool handleRefreshTap();
  bool handleChartPeriodToggleTap();
  bool handleBooksInput();
  void drawControlDrawer() const;
  void refresh() override;

  bool controlDrawerOpen_ = false;
  bool indexRefreshPending_ = false;

 public:
  Statistics(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> close);

  const char* name() const override { return "Statistics"; }

  void onEnter() override;
  void onExit() override;
  void loop() override;
};

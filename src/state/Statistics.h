#pragma once

/**
 * @file Statistics.h
 * @brief Public interface and types for Statistics.
 */

#include <cstdint>
#include <string>
#include <vector>

/**
 * Structure containing reading statistics for a single book.
 *
 * This structure tracks reading progress, time spent, pages read,
 * and other metrics for a specific EPUB book.
 */
struct BookReadingStats {
  std::string path;
  std::string title;
  std::string author;

  uint32_t totalReadingTimeMs;
  uint32_t totalPagesRead;
  uint32_t totalChaptersRead;
  uint32_t lastReadTimeMs;
  float progressPercent;
  uint16_t lastSpineIndex;
  uint16_t lastPageNumber;
  uint32_t avgPageTimeMs;
  uint32_t sessionCount;

  /**
   * Default constructor initializing all numeric fields to zero
   * and string fields to empty.
   */
  BookReadingStats()
      : path(""),
        title(""),
        author(""),
        totalReadingTimeMs(0),
        totalPagesRead(0),
        totalChaptersRead(0),
        lastReadTimeMs(0),
        progressPercent(0),
        lastSpineIndex(0),
        lastPageNumber(0),
        avgPageTimeMs(0),
        sessionCount(0) {}
};

/** Cached metadata used by the fast Statistics Books list. */
struct BookListMetadata {
  std::string thumbnailPath;
  std::string title;
  std::string description;
  float progressPercent = 0.0f;
};

/**
 * Structure containing aggregated reading statistics across all books.
 */
struct GlobalReadingStats {
  uint32_t totalBooksStarted;
  uint32_t totalBooksFinished;
  uint32_t totalReadingTimeMs;
  uint32_t totalPagesRead;
  uint32_t totalChaptersRead;
  uint32_t totalSessions;

  /**
   * Default constructor initializing all fields to zero.
   */
  GlobalReadingStats()
      : totalBooksStarted(0),
        totalBooksFinished(0),
        totalReadingTimeMs(0),
        totalPagesRead(0),
        totalChaptersRead(0),
        totalSessions(0) {}
};

/** A daily reading bucket keyed by the local hardware RTC date (YYYYMMDD). */
struct ReadingHistoryEntry {
  uint32_t dateKey = 0;
  uint32_t readingTimeMs = 0;
};

/** Reading time attributed to one cached book on one local RTC date. */
struct ReadingHistoryBookEntry {
  uint32_t dateKey = 0;
  uint32_t readingTimeMs = 0;
  std::string bookPath;
};

/**
 * Saves reading statistics for a book to its cache directory.
 *
 * @param cachePath Path to the book's cache directory
 * @param stats The book reading statistics to persist
 */
void saveBookStats(const char* cachePath, const BookReadingStats& stats);

/** Loads the last globally refreshed Books-list metadata aggregate. */
std::vector<BookListMetadata> loadBookListMetadata();

/** Replaces the persisted Books-list metadata aggregate. */
bool saveBookListMetadata(const std::vector<BookListMetadata>& books);

/**
 * Loads reading statistics for a book from its cache directory.
 *
 * @param cachePath Path to the book's cache directory
 * @param stats Reference to populate with loaded statistics
 * @return true if statistics were successfully loaded, false otherwise
 */
bool loadBookStats(const char* cachePath, BookReadingStats& stats);

/**
 * Retrieves reading statistics for all books with per-book stats on the SD card.
 *
 * Scans `/.metadata/epub` (EPUB caches) and `/.metadata/xtc` (XTC caches)
 * for `statistics.bin` files.
 *
 * @return Vector containing statistics for all books with valid stats files
 */
std::vector<BookReadingStats> getAllBooksStats();

/**
 * Retrieves reading statistics for a specific book.
 *
 * @param bookPath Path to the book's cache directory
 * @param stats Reference to populate with the book's statistics
 * @return true if statistics were successfully loaded, false otherwise
 */
bool getBookStats(const char* bookPath, BookReadingStats& stats);

/**
 * Loads global reading statistics from the statistics file.
 *
 * @param stats Reference to populate with global statistics
 * @return true if statistics were successfully loaded, false otherwise
 */
bool loadGlobalStats(GlobalReadingStats& stats);

/**
 * Saves global reading statistics to the statistics file.
 *
 * @param stats The global statistics to persist
 */
void saveGlobalStats(const GlobalReadingStats& stats);

/** Aggregates global totals from an already-built list of per-book stats (no SD rescan). */
GlobalReadingStats aggregateGlobalStatsFromBooks(const std::vector<BookReadingStats>& books);

/**
 * Recomputes global totals by scanning all per-book statistics files.
 */
GlobalReadingStats generateGlobalStats();

/** Adds elapsed reading time to the current RTC date bucket. */
void recordReadingHistoryMs(uint32_t elapsedMs);

/** Adds elapsed reading time and attributes it to the active book cache path. */
void recordReadingHistoryMs(uint32_t elapsedMs, const std::string& bookPath);

/** Returns accumulated reading time for a book in morning, noon, and evening buckets. */
void getBookReadingTimeOfDay(const std::string& bookPath, uint32_t& morningMs, uint32_t& noonMs,
                             uint32_t& eveningMs);

/** Flushes pending RTC history changes to /.system/statistics.bin. */
void saveReadingHistory();

/** Clears the in-memory statistics snapshots after their files are removed. */
void clearReadingStatisticsState();

/** Returns all retained daily RTC buckets, oldest first. */
const std::vector<ReadingHistoryEntry>& getReadingHistory();

/** Returns daily per-book reading records, oldest first. */
const std::vector<ReadingHistoryBookEntry>& getReadingHistoryBooks();

/** Returns the current local RTC date as YYYYMMDD, or false when unavailable. */
bool currentRtcDateKey(uint32_t& dateKey);

/** Converts a YYYYMMDD date key to a monotonic civil-day number. */
int64_t readingDateToDay(uint32_t dateKey);

/** Converts a monotonic civil-day number back to a YYYYMMDD date key. */
uint32_t readingDayToDate(int64_t day);

/**
 * @file Statistics.cpp
 * @brief Definitions for Statistics.
 */

#include "state/Statistics.h"

#include <Arduino.h>
#include <HalGPIO.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

extern HalGPIO gpio;

static const char* STATS_DIR = "/.system";
static const char* statistics_FILE = "/.system/statistics.bin";

constexpr uint32_t STATS_FILE_VERSION = 2;
constexpr uint32_t STATS_MAGIC_NUMBER = 0x53544154;
constexpr uint32_t GLOBAL_STATS_FILE_VERSION = 3;
constexpr size_t MAX_READING_HISTORY_ENTRIES = 370;

GlobalReadingStats cachedGlobalStats;
std::vector<ReadingHistoryEntry> readingHistory;
bool globalStatsLoaded = false;
bool readingHistoryDirty = false;

/**
 * RAII wrapper for FsFile that ensures file is closed when object goes out of scope.
 */
class FileGuard {
 private:
  FsFile& file;

 public:
  explicit FileGuard(FsFile& f) : file(f) {}
  ~FileGuard() {
    if (file) {
      file.close();
    }
  }
  FileGuard(const FileGuard&) = delete;
  FileGuard& operator=(const FileGuard&) = delete;
};

uint32_t makeDateKey(const HalGPIO::DateTime& dateTime) {
  return static_cast<uint32_t>(dateTime.year) * 10000UL + static_cast<uint32_t>(dateTime.month) * 100UL +
         dateTime.day;
}

bool currentRtcDateKey(uint32_t& dateKey) {
#ifdef SIMULATOR
  (void)dateKey;
  return false;
#else
  HalGPIO::DateTime dateTime;
  if (!gpio.readDateTime(dateTime) || dateTime.year < 2000 || dateTime.month < 1 || dateTime.month > 12 ||
      dateTime.day < 1 || dateTime.day > 31) {
    return false;
  }
  dateKey = makeDateKey(dateTime);
  return true;
#endif
}

// Howard Hinnant's civil calendar conversion, using only integer arithmetic.
int64_t civilToDay(const int year, unsigned month, const unsigned day) {
  int adjustedYear = year - (month <= 2);
  const int era = (adjustedYear >= 0 ? adjustedYear : adjustedYear - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(adjustedYear - era * 400);
  const unsigned monthOfYear = static_cast<unsigned>(static_cast<int>(month) + (month > 2 ? -3 : 9));
  const unsigned dayOfYear = (153 * monthOfYear + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
}

uint32_t dayToCivil(const int64_t day) {
  const int64_t shifted = day + 719468;
  const int era = static_cast<int>((shifted >= 0 ? shifted : shifted - 146096) / 146097);
  const unsigned dayOfEra = static_cast<unsigned>(shifted - static_cast<int64_t>(era) * 146097);
  const unsigned dayOfYear = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  const int year = era * 400 + static_cast<int>(dayOfYear);
  const unsigned dayOfYearAdjusted = dayOfEra - (365 * dayOfYear + dayOfYear / 4 - dayOfYear / 100);
  const unsigned month = (5 * dayOfYearAdjusted + 2) / 153;
  const unsigned dayOfMonth = dayOfYearAdjusted - (153 * month + 2) / 5 + 1;
  const int actualYear = year + (month >= 10 ? 1 : 0);
  const unsigned actualMonth = static_cast<unsigned>(static_cast<int>(month) + (month < 10 ? 3 : -9));
  return static_cast<uint32_t>(actualYear) * 10000UL + actualMonth * 100UL + dayOfMonth;
}

bool loadGlobalFile(GlobalReadingStats& stats) {
  memset(&stats, 0, sizeof(stats));
  readingHistory.clear();

  FsFile file;
  FileGuard guard(file);
  if (!SdMan.openFileForRead("STATS", statistics_FILE, file)) return false;

  uint32_t firstWord = 0;
  if (file.read(&firstWord, sizeof(firstWord)) != sizeof(firstWord)) return false;

  if (firstWord == STATS_MAGIC_NUMBER) {
    uint32_t version = 0;
    uint32_t entryCount = 0;
    if (file.read(&version, sizeof(version)) != sizeof(version) || version != GLOBAL_STATS_FILE_VERSION ||
        file.read(&stats, sizeof(stats)) != sizeof(stats) ||
        file.read(&entryCount, sizeof(entryCount)) != sizeof(entryCount) ||
        entryCount > MAX_READING_HISTORY_ENTRIES) {
      return false;
    }
    readingHistory.reserve(entryCount);
    for (uint32_t i = 0; i < entryCount; ++i) {
      ReadingHistoryEntry entry;
      if (file.read(&entry, sizeof(entry)) != sizeof(entry)) {
        readingHistory.clear();
        return false;
      }
      if (entry.dateKey != 0 && entry.readingTimeMs != 0) readingHistory.push_back(entry);
    }
    return true;
  }

  // Version 1/2 stored GlobalReadingStats as a raw struct without a header.
  memcpy(&stats, &firstWord, sizeof(firstWord));
  return file.read(reinterpret_cast<uint8_t*>(&stats) + sizeof(firstWord), sizeof(stats) - sizeof(firstWord)) ==
         sizeof(stats) - sizeof(firstWord);
}

/**
 * Saves reading statistics for a specific book to a binary file in its cache directory.
 * Uses a temporary file and atomic rename to prevent corruption.
 *
 * @param cachePath Path to the book's cache directory
 * @param stats The book reading statistics to save
 */
void saveBookStats(const char* cachePath, const BookReadingStats& stats) {
  if (!cachePath) return;
  std::string statsPath = std::string(cachePath) + "/statistics.bin";
  std::string tempPath = statsPath + ".tmp";

  bool writeSuccess = false;

  {
    FsFile file;
    FileGuard guard(file);

    if (SdMan.openFileForWrite("STATS", tempPath.c_str(), file)) {
      uint32_t magic = STATS_MAGIC_NUMBER;
      uint32_t version = STATS_FILE_VERSION;

      file.write(&magic, sizeof(uint32_t));
      file.write(&version, sizeof(uint32_t));

      file.write(&stats.totalReadingTimeMs, sizeof(uint32_t));
      file.write(&stats.totalPagesRead, sizeof(uint32_t));
      file.write(&stats.totalChaptersRead, sizeof(uint32_t));
      file.write(&stats.lastReadTimeMs, sizeof(uint32_t));
      file.write(&stats.progressPercent, sizeof(float));
      file.write(&stats.lastSpineIndex, sizeof(uint16_t));
      file.write(&stats.lastPageNumber, sizeof(uint16_t));
      file.write(&stats.avgPageTimeMs, sizeof(uint32_t));
      file.write(&stats.sessionCount, sizeof(uint32_t));

      uint32_t pathLength = stats.path.length();
      file.write(&pathLength, sizeof(uint32_t));
      if (pathLength > 0) file.write(stats.path.c_str(), pathLength);

      uint32_t titleLength = stats.title.length();
      file.write(&titleLength, sizeof(uint32_t));
      if (titleLength > 0) file.write(stats.title.c_str(), titleLength);

      uint32_t authorLength = stats.author.length();
      file.write(&authorLength, sizeof(uint32_t));
      if (authorLength > 0) file.write(stats.author.c_str(), authorLength);

      writeSuccess = true;
    }
  }

  if (writeSuccess) {
    SdMan.remove(statsPath.c_str());
    if (!SdMan.rename(tempPath.c_str(), statsPath.c_str())) {
      SdMan.remove(tempPath.c_str());
    }
  } else {
    SdMan.remove(tempPath.c_str());
  }
}

/**
 * Loads reading statistics for a specific book from its cache directory.
 * Validates file format using magic number and version.
 *
 * @param cachePath Path to the book's cache directory
 * @param stats Reference to populate with loaded statistics
 * @return true if statistics were successfully loaded, false otherwise
 */
bool loadBookStats(const char* cachePath, BookReadingStats& stats) {
  if (!cachePath) return false;
  std::string statsPath = std::string(cachePath) + "/statistics.bin";

  FsFile file;
  FileGuard guard(file);

  if (!SdMan.openFileForRead("STATS", statsPath.c_str(), file)) {
    return false;
  }

  uint32_t magic, version;
  if (file.read(&magic, sizeof(uint32_t)) != sizeof(uint32_t) || magic != STATS_MAGIC_NUMBER) {
    return false;
  }

  if (file.read(&version, sizeof(uint32_t)) != sizeof(uint32_t)) {
    return false;
  }

  file.read(&stats.totalReadingTimeMs, sizeof(uint32_t));
  file.read(&stats.totalPagesRead, sizeof(uint32_t));
  file.read(&stats.totalChaptersRead, sizeof(uint32_t));
  file.read(&stats.lastReadTimeMs, sizeof(uint32_t));
  file.read(&stats.progressPercent, sizeof(float));
  file.read(&stats.lastSpineIndex, sizeof(uint16_t));
  file.read(&stats.lastPageNumber, sizeof(uint16_t));
  file.read(&stats.avgPageTimeMs, sizeof(uint32_t));

  if (version >= 2) {
    file.read(&stats.sessionCount, sizeof(uint32_t));
  } else {
    stats.sessionCount = 0;
  }

  uint32_t pathLen;
  if (file.read(&pathLen, sizeof(uint32_t)) == sizeof(uint32_t) && pathLen < 512) {
    if (pathLen > 0) {
      stats.path.resize(pathLen);
      file.read(&stats.path[0], pathLen);
    } else {
      stats.path.clear();
    }
  }

  uint32_t titleLen;
  if (file.read(&titleLen, sizeof(uint32_t)) == sizeof(uint32_t) && titleLen < 512) {
    if (titleLen > 0) {
      stats.title.resize(titleLen);
      file.read(&stats.title[0], titleLen);
    } else {
      stats.title.clear();
    }
  }

  uint32_t authorLen;
  if (file.read(&authorLen, sizeof(uint32_t)) == sizeof(uint32_t) && authorLen < 512) {
    if (authorLen > 0) {
      stats.author.resize(authorLen);
      file.read(&stats.author[0], authorLen);
    } else {
      stats.author.clear();
    }
  }

  return true;
}

namespace {

void appendStatsFromCacheDir(std::vector<BookReadingStats>& result, const char* rootDir,
                             bool (*acceptName)(const char* name)) {
  FsFile root;
  FileGuard rootGuard(root);

  root = SdMan.open(rootDir);
  if (!root || !root.isDirectory()) {
    return;
  }

  root.rewindDirectory();
  char fileName[128];

  while (true) {
    FsFile entry;
    FileGuard entryGuard(entry);

    entry = root.openNextFile();
    if (!entry) {
      break;
    }

    if (!entry.isDirectory()) {
      continue;
    }

    entry.getName(fileName, sizeof(fileName));
    if (acceptName != nullptr && !acceptName(fileName)) {
      continue;
    }

    const std::string path = std::string(rootDir) + "/" + std::string(fileName);
    BookReadingStats stats;
    if (loadBookStats(path.c_str(), stats)) {
      stats.path = path;
      result.push_back(stats);
    }
  }
}

}  // namespace

/**
 * Retrieves reading statistics for all books in the EPUB cache directory.
 * Scans through all subdirectories in /.metadata/epub and loads stats for each.
 *
 * @return Vector containing statistics for all books that have saved stats
 */
std::vector<BookReadingStats> getAllBooksStats() {
  std::vector<BookReadingStats> result;
  result.reserve(24);

  appendStatsFromCacheDir(result, "/.metadata/epub", nullptr);
  appendStatsFromCacheDir(result, "/.metadata/xtc", nullptr);

  return result;
}

/**
 * Retrieves reading statistics for a specific book.
 *
 * @param bookPath Path to the book's cache directory
 * @param stats Reference to populate with the book's statistics
 * @return true if statistics were successfully loaded, false otherwise
 */
bool getBookStats(const char* bookPath, BookReadingStats& stats) { return loadBookStats(bookPath, stats); }

/**
 * Loads global reading statistics from the main statistics file.
 * Zeros out the stats struct if read fails.
 *
 * @param stats Reference to populate with global statistics
 * @return true if statistics were successfully loaded, false otherwise
 */
bool loadGlobalStats(GlobalReadingStats& stats) {
  if (globalStatsLoaded) {
    stats = cachedGlobalStats;
    return true;
  }

  const bool loaded = loadGlobalFile(stats);
  cachedGlobalStats = stats;
  globalStatsLoaded = true;
  return loaded;
}

/**
 * Saves global reading statistics to the main statistics file.
 * Uses a temporary file and atomic rename to prevent corruption.
 * Creates the statistics directory if it doesn't exist.
 *
 * @param stats Global statistics to save
 */
void saveGlobalStats(const GlobalReadingStats& stats) {
  if (!globalStatsLoaded) {
    GlobalReadingStats existing;
    loadGlobalFile(existing);
    globalStatsLoaded = true;
  }
  cachedGlobalStats = stats;
  SdMan.mkdir(STATS_DIR);  // same root as settings.bin, wifi.bin, etc.
  const std::string tempPath = std::string(statistics_FILE) + ".tmp";
  bool writeOk = false;
  {
    FsFile file;
    FileGuard guard(file);
    if (SdMan.openFileForWrite("STATS", tempPath.c_str(), file)) {
      const uint32_t magic = STATS_MAGIC_NUMBER;
      const uint32_t version = GLOBAL_STATS_FILE_VERSION;
      const uint32_t entryCount = static_cast<uint32_t>(readingHistory.size());
      writeOk = file.write(&magic, sizeof(magic)) == sizeof(magic) &&
                file.write(&version, sizeof(version)) == sizeof(version) &&
                file.write(&stats, sizeof(stats)) == sizeof(stats) &&
                file.write(&entryCount, sizeof(entryCount)) == sizeof(entryCount);
      for (const ReadingHistoryEntry& entry : readingHistory) {
        if (!writeOk || file.write(&entry, sizeof(entry)) != sizeof(entry)) {
          writeOk = false;
          break;
        }
      }
    }
  }
  if (writeOk) {
    SdMan.remove(statistics_FILE);
    if (!SdMan.rename(tempPath.c_str(), statistics_FILE)) {
      SdMan.remove(tempPath.c_str());
    } else {
      readingHistoryDirty = false;
    }
  } else {
    SdMan.remove(tempPath.c_str());
  }
}

/**
 * Generates global reading statistics by aggregating all individual book statistics.
 * Calculates totals for books started, finished, reading time, pages, chapters, and sessions.
 *
 * @return Aggregated global reading statistics
 */
GlobalReadingStats aggregateGlobalStatsFromBooks(const std::vector<BookReadingStats>& allBooks) {
  GlobalReadingStats global;
  memset(&global, 0, sizeof(GlobalReadingStats));
  for (const auto& book : allBooks) {
    global.totalBooksStarted++;
    global.totalReadingTimeMs += book.totalReadingTimeMs;
    global.totalPagesRead += book.totalPagesRead;
    global.totalChaptersRead += book.totalChaptersRead;
    global.totalSessions += book.sessionCount;
    if (book.progressPercent >= 99.0f) {
      global.totalBooksFinished++;
    }
  }
  return global;
}

GlobalReadingStats generateGlobalStats() { return aggregateGlobalStatsFromBooks(getAllBooksStats()); }

void recordReadingHistoryMs(const uint32_t elapsedMs) {
  if (elapsedMs == 0) return;

  uint32_t today = 0;
  if (!currentRtcDateKey(today)) return;

  if (!globalStatsLoaded) {
    GlobalReadingStats ignored;
    loadGlobalStats(ignored);
  }

  auto it = std::find_if(readingHistory.begin(), readingHistory.end(),
                         [today](const ReadingHistoryEntry& entry) { return entry.dateKey == today; });
  if (it == readingHistory.end()) {
    readingHistory.push_back({today, elapsedMs});
  } else {
    const uint32_t room = UINT32_MAX - it->readingTimeMs;
    it->readingTimeMs += std::min(room, elapsedMs);
  }

  std::sort(readingHistory.begin(), readingHistory.end(),
            [](const ReadingHistoryEntry& lhs, const ReadingHistoryEntry& rhs) {
              return readingDateToDay(lhs.dateKey) < readingDateToDay(rhs.dateKey);
            });
  if (readingHistory.size() > MAX_READING_HISTORY_ENTRIES) {
    readingHistory.erase(readingHistory.begin(),
                         readingHistory.begin() + (readingHistory.size() - MAX_READING_HISTORY_ENTRIES));
  }
  readingHistoryDirty = true;
}

void saveReadingHistory() {
  if (!readingHistoryDirty) return;
  if (!globalStatsLoaded) {
    GlobalReadingStats ignored;
    loadGlobalStats(ignored);
  }
  saveGlobalStats(cachedGlobalStats);
}

const std::vector<ReadingHistoryEntry>& getReadingHistory() {
  if (!globalStatsLoaded) {
    GlobalReadingStats ignored;
    loadGlobalStats(ignored);
  }
  return readingHistory;
}

int64_t readingDateToDay(const uint32_t dateKey) {
  const int year = static_cast<int>(dateKey / 10000UL);
  const unsigned month = static_cast<unsigned>((dateKey / 100UL) % 100UL);
  const unsigned day = static_cast<unsigned>(dateKey % 100UL);
  if (year < 1 || month < 1 || month > 12 || day < 1 || day > 31) return 0;
  return civilToDay(year, month, day);
}

uint32_t readingDayToDate(const int64_t day) { return dayToCivil(day); }

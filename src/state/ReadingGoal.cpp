#include "state/ReadingGoal.h"

#include <HalGPIO.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdint>

#include "state/ReaderSetting.h"
#include "state/Statistics.h"

extern HalGPIO gpio;

namespace ReadingGoal {
namespace {

constexpr uint32_t kMagic = 0x52474F41;  // RGOA
constexpr uint8_t kVersion = 1;
constexpr char kPath[] = "/.system/reading_goal.bin";
constexpr char kTempPath[] = "/.system/reading_goal.bin.tmp";

uint32_t storedDateKey = 0;
uint32_t storedReadingTimeMs = 0;
bool loaded = false;
bool dirty = false;

uint32_t makeDateKey(const HalGPIO::DateTime& dateTime) {
  return static_cast<uint32_t>(dateTime.year) * 10000UL + static_cast<uint32_t>(dateTime.month) * 100UL +
         dateTime.day;
}

bool readRtcDate(uint32_t& outDateKey) {
#ifdef SIMULATOR
  (void)outDateKey;
  return false;
#else
  HalGPIO::DateTime dateTime;
  if (!gpio.readDateTime(dateTime) || dateTime.year < 2000 || dateTime.month < 1 || dateTime.month > 12 ||
      dateTime.day < 1 || dateTime.day > 31) {
    return false;
  }
  outDateKey = makeDateKey(dateTime);
  return true;
#endif
}

void load() {
  if (loaded) return;
  loaded = true;

  FsFile file;
  if (!SdMan.openFileForRead("RGOAL", kPath, file)) return;

  uint32_t magic = 0;
  uint8_t version = 0;
  uint8_t reserved[3] = {};
  const bool valid = file.read(&magic, sizeof(magic)) == sizeof(magic) &&
                     file.read(&version, sizeof(version)) == sizeof(version) &&
                     file.read(reserved, sizeof(reserved)) == sizeof(reserved) &&
                     file.read(&storedDateKey, sizeof(storedDateKey)) == sizeof(storedDateKey) &&
                     file.read(&storedReadingTimeMs, sizeof(storedReadingTimeMs)) == sizeof(storedReadingTimeMs) &&
                     magic == kMagic && version == kVersion;
  file.close();
  if (!valid) {
    storedDateKey = 0;
    storedReadingTimeMs = 0;
  }
}

void syncDate() {
  load();
  uint32_t today = 0;
  if (!readRtcDate(today)) return;
  if (storedDateKey == today) return;
  storedDateKey = today;
  storedReadingTimeMs = 0;
  dirty = true;
}

}  // namespace

Status status() {
  load();
  uint32_t today = 0;
  const bool available = readRtcDate(today);
  if (available) syncDate();

  Status result;
  result.rtcAvailable = available;
  result.dateKey = available ? today : 0;
  result.readingTimeMs = available && storedDateKey == today ? storedReadingTimeMs : 0;
  result.goalMinutes = READER_SETTINGS.dailyReadingGoalMinutes;
  return result;
}

void recordReadingMs(const uint32_t elapsedMs) {
  if (elapsedMs == 0) return;
  syncDate();

  uint32_t today = 0;
  if (!readRtcDate(today) || storedDateKey != today) return;
  const uint32_t room = UINT32_MAX - storedReadingTimeMs;
  storedReadingTimeMs += std::min(room, elapsedMs);
  recordReadingHistoryMs(elapsedMs);
  dirty = true;
}

void save() {
  load();
  if (dirty) {
    SdMan.mkdir("/.system");

    FsFile file;
    if (SdMan.openFileForWrite("RGOAL", kTempPath, file)) {
      const uint32_t magic = kMagic;
      const uint8_t version = kVersion;
      const uint8_t reserved[3] = {};
      const bool ok = file.write(&magic, sizeof(magic)) == sizeof(magic) &&
                      file.write(&version, sizeof(version)) == sizeof(version) &&
                      file.write(reserved, sizeof(reserved)) == sizeof(reserved) &&
                      file.write(&storedDateKey, sizeof(storedDateKey)) == sizeof(storedDateKey) &&
                      file.write(&storedReadingTimeMs, sizeof(storedReadingTimeMs)) == sizeof(storedReadingTimeMs);
      file.close();
      if (ok) {
        SdMan.remove(kPath);
        if (SdMan.rename(kTempPath, kPath)) dirty = false;
      } else {
        SdMan.remove(kTempPath);
      }
    }
  }
  saveReadingHistory();
}

void clear() {
  load();
  storedDateKey = 0;
  storedReadingTimeMs = 0;
  dirty = false;
  SdMan.remove(kTempPath);
  SdMan.remove(kPath);
}

}  // namespace ReadingGoal

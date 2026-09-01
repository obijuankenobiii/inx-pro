#pragma once

#include <cstdint>
#include <string>

namespace ReadingGoal {

struct Status {
  bool rtcAvailable = false;
  uint32_t dateKey = 0;
  uint32_t readingTimeMs = 0;
  uint8_t goalMinutes = 0;
};

/** Returns today's accumulated reading time, keyed by the hardware RTC date. */
Status status();

/** Adds a completed reading interval to today's RTC bucket. */
void recordReadingMs(uint32_t elapsedMs);

/** Adds a reading interval and attributes it to the active book cache path. */
void recordReadingMs(uint32_t elapsedMs, const std::string& bookPath);

/** Persists any accumulated reading time that has not yet been flushed. */
void save();

/** Clears the accumulated reading time for the current daily-reading bucket. */
void clear();

}  // namespace ReadingGoal

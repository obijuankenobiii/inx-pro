#pragma once

#include <cstdint>
#include <cstring>
#include <ctime>
#include <initializer_list>

namespace clock_time {

inline bool parseUtcOffset(const char* value, int& quarterOffset) {
  if (!value || std::strlen(value) != 6 || (value[0] != '+' && value[0] != '-') || value[3] != ':') return false;
  for (const int index : {1, 2, 4, 5}) {
    if (value[index] < '0' || value[index] > '9') return false;
  }
  const int hours = (value[1] - '0') * 10 + value[2] - '0';
  const int minutes = (value[4] - '0') * 10 + value[5] - '0';
  const int totalMinutes = (value[0] == '-' ? -1 : 1) * (hours * 60 + minutes);
  if (minutes >= 60 || totalMinutes < -12 * 60 || totalMinutes > 14 * 60 || totalMinutes % 15 != 0) return false;
  quarterOffset = totalMinutes / 15 + 48;
  return true;
}

// The external RTC stores local calendar fields. Apply the configured offset
// exactly once without depending on the C library's process-wide TZ setting.
template <typename DateTime>
bool toRtcDateTime(time_t utcNow, int offsetMinutes, DateTime& out) {
  if (utcNow < 1704067200 || offsetMinutes < -720 || offsetMinutes > 840) return false;
  const time_t localEpoch = utcNow + offsetMinutes * 60;
  tm local{};
  if (!gmtime_r(&localEpoch, &local)) return false;
  out.year = static_cast<uint16_t>(local.tm_year + 1900);
  out.month = static_cast<uint8_t>(local.tm_mon + 1);
  out.day = static_cast<uint8_t>(local.tm_mday);
  out.hour = static_cast<uint8_t>(local.tm_hour);
  out.minute = static_cast<uint8_t>(local.tm_min);
  out.second = static_cast<uint8_t>(local.tm_sec);
  out.weekday = static_cast<uint8_t>(local.tm_wday == 0 ? 7 : local.tm_wday);
  return true;
}

template <typename DateTime>
bool sameDateTime(const DateTime& a, const DateTime& b) {
  return a.year == b.year && a.month == b.month && a.day == b.day && a.hour == b.hour && a.minute == b.minute &&
         a.second == b.second;
}

// Permit the clock to tick during the write/read, including midnight rollover.
template <typename DateTime>
bool verifyRtcDateTime(const DateTime& stored, time_t writtenUtc, int offsetMinutes, uint32_t elapsedMs) {
  const uint32_t elapsedSeconds = elapsedMs / 1000;
  DateTime expected{};
  for (uint32_t seconds = elapsedSeconds; seconds <= elapsedSeconds + 1; ++seconds) {
    if (toRtcDateTime(writtenUtc + seconds, offsetMinutes, expected) && sameDateTime(stored, expected)) return true;
  }
  return false;
}

}  // namespace clock_time

#include "system/ClockTime.h"
#include "system/NtpTimeSync.h"

#include <esp_sntp.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

namespace {
uint32_t uptime = 0;
uint32_t start = 0;
uint32_t respondAfterMs = 0;
time_t systemEpoch = 0;
time_t responseEpoch = 0;
bool enabled = false;
bool respond = false;
bool received = false;
sntp_sync_status_t status = SNTP_SYNC_STATUS_RESET;
sntp_sync_mode_t mode = SNTP_SYNC_MODE_SMOOTH;
std::string servers[2];

struct DateTime {
  uint16_t year;
  uint8_t month, day, hour, minute, second, weekday;
};

void reset(time_t epoch, bool answer, uint32_t delay = 300, time_t reply = 1794268800) {
  uptime = 0;
  systemEpoch = epoch;
  respond = answer;
  respondAfterMs = delay;
  responseEpoch = reply;
  received = false;
  enabled = false;
  status = SNTP_SYNC_STATUS_RESET;
  mode = SNTP_SYNC_MODE_SMOOTH;
}
}  // namespace

unsigned long millis() { return uptime; }
void vTaskDelay(uint32_t ms) { uptime += ms; }
extern "C" time_t time(time_t* out) {
  if (out) *out = systemEpoch;
  return systemEpoch;
}
bool esp_sntp_enabled() { return enabled; }
void esp_sntp_stop() { enabled = false; }
void sntp_set_sync_status(sntp_sync_status_t value) { status = value; }
void esp_sntp_set_sync_mode(sntp_sync_mode_t value) { mode = value; }
void esp_sntp_setoperatingmode(esp_sntp_operatingmode_t value) { assert(value == ESP_SNTP_OPMODE_POLL); }
void esp_sntp_setservername(int index, const char* name) { servers[index] = name; }
void esp_sntp_init() {
  assert(status == SNTP_SYNC_STATUS_RESET);
  start = uptime;
  enabled = true;
}
sntp_sync_status_t sntp_get_sync_status() {
  if (enabled && respond && !received && uptime - start >= respondAfterMs) {
    received = true;
    systemEpoch = responseEpoch;
    status = SNTP_SYNC_STATUS_COMPLETED;
  }
  const auto current = status;
  if (status == SNTP_SYNC_STATUS_COMPLETED) status = SNTP_SYNC_STATUS_RESET;
  return current;
}

int main() {
  time_t synced = 123;
  // Regression: a plausible old clock must not pass without a new response.
  reset(1794000000, false);
  assert(!syncSystemClockWithNtp(synced, 500));
  assert(synced == 123 && uptime >= 500 && !enabled);

  // Completion from an earlier service must be reset before starting this one.
  reset(1794000000, false);
  enabled = true;
  status = SNTP_SYNC_STATUS_COMPLETED;
  assert(!syncSystemClockWithNtp(synced, 500));

  // The completion getter resets itself; consume it only once.
  reset(1794000000, true);
  assert(syncSystemClockWithNtp(synced, 500));
  assert(received && uptime == 300 && synced == responseEpoch && !enabled);
  assert(mode == SNTP_SYNC_MODE_IMMED && servers[0] == "pool.ntp.org" && servers[1] == "time.google.com");

  reset(0, true);
  assert(syncSystemClockWithNtp(synced, 500) && synced == responseEpoch);
  reset(1794000000, true, 100, 0);
  synced = 123;
  assert(!syncSystemClockWithNtp(synced, 500) && synced == 123);

  // A long-running device can wrap millis() while waiting for a reply.
  reset(1794000000, true);
  uptime = std::numeric_limits<uint32_t>::max() - 100;
  assert(syncSystemClockWithNtp(synced, 500) && synced == responseEpoch);

  int offset = -1;
  assert(clock_time::parseUtcOffset("+08:00", offset) && offset == 80);
  assert(clock_time::parseUtcOffset("-03:30", offset) && offset == 34);
  assert(clock_time::parseUtcOffset("+05:45", offset) && offset == 71);
  assert(clock_time::parseUtcOffset("-12:00", offset) && offset == 0);
  assert(clock_time::parseUtcOffset("+14:00", offset) && offset == 104);
  for (const char* bad : {"", "+", "+08", "+01:75", "+14:15", "-12:15", "+00:01", "UTC+08:00"}) {
    assert(!clock_time::parseUtcOffset(bad, offset));
  }
  assert(!clock_time::parseUtcOffset(nullptr, offset));

  // UTC 2024-01-01 00:00:00, with one application of the chosen offset.
  constexpr time_t utc = 1704067200;
  DateTime dt{};
  setenv("TZ", "UTC0", 1);
  tzset();
  assert(clock_time::toRtcDateTime(utc, 480, dt));
  assert(dt.year == 2024 && dt.month == 1 && dt.day == 1 && dt.hour == 8 && dt.weekday == 1);
  setenv("TZ", "EST5EDT", 1);
  tzset();
  DateTime same{};
  assert(clock_time::toRtcDateTime(utc, 480, same) && clock_time::sameDateTime(dt, same));
  assert(clock_time::toRtcDateTime(utc, -210, dt));
  assert(dt.year == 2023 && dt.month == 12 && dt.day == 31 && dt.hour == 20 && dt.minute == 30 && dt.weekday == 7);
  assert(clock_time::toRtcDateTime(utc, 345, dt) && dt.hour == 5 && dt.minute == 45);

  // Verification accepts normal ticking across a minute/day/year rollover.
  constexpr time_t rollover = utc + 366 * 86400;  // 2025-01-01
  assert(clock_time::toRtcDateTime(rollover - 1, 0, dt));
  assert(clock_time::verifyRtcDateTime(dt, rollover - 1, 0, 0));
  assert(clock_time::toRtcDateTime(rollover, 0, dt));
  assert(clock_time::verifyRtcDateTime(dt, rollover - 1, 0, 200));
  assert(clock_time::toRtcDateTime(rollover + 5, 0, dt));
  assert(!clock_time::verifyRtcDateTime(dt, rollover - 1, 0, 200));
  assert(!clock_time::verifyRtcDateTime(dt, rollover + 5, 480, 0));
  std::puts("Clock sync regression checks passed");
}

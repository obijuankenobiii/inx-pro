#include "system/ClockTime.h"

#include <BoardConfig.h>
#include <Rtc.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

namespace {
std::array<uint8_t, 256> registers{};
std::vector<uint8_t> timeWrite;
bool present = true;
}

bool hostI2cWrite(uint8_t addr, const std::vector<uint8_t>& bytes) {
  if (!present || addr != 0x51 || bytes.empty()) return false;
  if (bytes[0] == 0x02) timeWrite = bytes;
  for (size_t i = 1; i < bytes.size(); ++i) registers[bytes[0] + i - 1] = bytes[i];
  return true;
}

bool hostI2cRead(uint8_t addr, uint8_t reg, uint8_t* out, size_t size) {
  if (!present || addr != 0x51) return false;
  for (size_t i = 0; i < size; ++i) out[i] = registers[reg + i];
  return true;
}

int main() {
  auto& config = BoardConfig::ACTIVE.sensors;
  config.i2cSda = 7;
  config.i2cScl = 8;
  config.i2cHz = 400000;
  config.rtcAddr = 0x51;
  config.i2cBus = 0;
  config.rtcType = BoardConfig::RtcType::Pcf8563;
  Rtc rtc;
  assert(rtc.begin());

  constexpr time_t utc = 1704067200;  // 2024-01-01 00:00 UTC
  Rtc::DateTime local{};
  assert(clock_time::toRtcDateTime(utc, 345, local));
  local.weekday = local.weekday == 7 ? 0 : local.weekday;
  assert(rtc.set(local));
  const std::vector<uint8_t> expected{0x02, 0x00, 0x45, 0x05, 0x01, 0x01, 0x01, 0x24};
  assert(timeWrite == expected);

  Rtc::DateTime stored{};
  assert(rtc.now(stored));
  assert(clock_time::verifyRtcDateTime(stored, utc, 345, 0));
  assert(!clock_time::verifyRtcDateTime(stored, utc, 0, 0));

  // The PCF8563 voltage-low flag makes its time untrustworthy until set again.
  registers[0x02] |= 0x80;
  assert(!rtc.now(stored));
  assert(rtc.set(local) && rtc.now(stored));
  present = false;
  assert(!rtc.now(stored) && !rtc.set(local));
  std::puts("Picco PCF8563 sync/read-back checks passed");
}

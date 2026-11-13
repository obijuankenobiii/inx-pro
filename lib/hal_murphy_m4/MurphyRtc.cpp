#include "MurphyRtc.h"

#include <BoardConfig.h>

#include "MurphyM4I2c.h"

namespace {
constexpr uint8_t kRtcAddress = 0x32;
constexpr uint8_t kTimeRegister = 0x10;
constexpr uint8_t kFlagRegister = 0x1E;
constexpr uint8_t kVoltageLowFlag = 0x02;

uint8_t bcdToDec(const uint8_t value) {
  return static_cast<uint8_t>((value >> 4) * 10U + (value & 0x0FU));
}

uint8_t decToBcd(const uint8_t value) {
  return static_cast<uint8_t>((value / 10U) << 4 | (value % 10U));
}

bool validDateTime(const MurphyRtc::DateTime& dateTime) {
  return dateTime.year >= 2000 && dateTime.year <= 2099 && dateTime.month >= 1 && dateTime.month <= 12 &&
         dateTime.day >= 1 && dateTime.day <= 31 && dateTime.hour <= 23 && dateTime.minute <= 59 &&
         dateTime.second <= 59;
}

i2c_master_dev_handle_t rtcDevice() {
  const auto& touch = BoardConfig::ACTIVE.touch;
  return murphy_m4_i2c::rtcDevice(touch.sda, touch.scl, kRtcAddress);
}

bool readRegister(const uint8_t reg, uint8_t& value) {
  value = 0;
  return murphy_m4_i2c::read(rtcDevice(), reg, &value, 1);
}

}  // namespace

bool MurphyRtc::begin() {
  uint8_t flags = 0;
  begun_ = readRegister(kFlagRegister, flags);
  return begun_;
}

bool MurphyRtc::now(DateTime& out) {
  if (!begun_) return false;

  uint8_t flags = 0;
  if (!readRegister(kFlagRegister, flags) || (flags & kVoltageLowFlag)) return false;

  uint8_t raw[7] = {};
  if (!murphy_m4_i2c::read(rtcDevice(), kTimeRegister, raw, sizeof(raw))) return false;

  out.second = bcdToDec(raw[0] & 0x7FU);
  out.minute = bcdToDec(raw[1] & 0x7FU);
  out.hour = bcdToDec(raw[2] & 0x3FU);
  out.weekday = 0;
  for (uint8_t day = 0; day < 7; ++day) {
    if (raw[3] & (1U << day)) {
      out.weekday = day;
      break;
    }
  }
  out.day = bcdToDec(raw[4] & 0x3FU);
  out.month = bcdToDec(raw[5] & 0x1FU);
  out.year = static_cast<uint16_t>(2000 + bcdToDec(raw[6]));
  return validDateTime(out);
}

bool MurphyRtc::set(const DateTime& dateTime) {
  if (!begun_ || !validDateTime(dateTime)) return false;

  const uint8_t payload[] = {
      kTimeRegister,
      decToBcd(dateTime.second),
      decToBcd(dateTime.minute),
      decToBcd(dateTime.hour),
      static_cast<uint8_t>(1U << (dateTime.weekday % 7U)),
      decToBcd(dateTime.day),
      decToBcd(dateTime.month),
      decToBcd(static_cast<uint8_t>(dateTime.year % 100)),
  };
  if (!murphy_m4_i2c::write(rtcDevice(), payload, sizeof(payload))) return false;

  uint8_t flags = 0;
  if (!readRegister(kFlagRegister, flags)) return false;
  flags = static_cast<uint8_t>(flags & ~kVoltageLowFlag);
  const uint8_t clearVlf[] = {kFlagRegister, flags};
  return murphy_m4_i2c::write(rtcDevice(), clearVlf, sizeof(clearVlf));
}

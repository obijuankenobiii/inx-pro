#pragma once

#include <cstdint>

class MurphyRtc {
 public:
  struct DateTime {
    uint16_t year = 2000;
    uint8_t month = 1;
    uint8_t day = 1;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t weekday = 0;  // 0=Sunday .. 6=Saturday
  };

  bool begin();
  bool now(DateTime& out);
  bool set(const DateTime& dateTime);

 private:
  bool begun_ = false;
};

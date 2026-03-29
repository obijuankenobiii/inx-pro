#include "Clock.h"

#include <ClockRender.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>

#include "state/SystemSetting.h"

extern HalGPIO gpio;

bool Clock::readDateTime(uint64_t& key) const {
#ifdef SIMULATOR
  (void)key;
  return false;
#else
  HalGPIO::DateTime dateTime;
  if (!gpio.readDateTime(dateTime)) return false;
  key = static_cast<uint64_t>(dateTime.year) * 100000000ULL +
        static_cast<uint64_t>(dateTime.month) * 1000000ULL + static_cast<uint64_t>(dateTime.day) * 10000ULL +
        static_cast<uint64_t>(dateTime.hour) * 100ULL + dateTime.minute;
  return true;
#endif
}

void Clock::render(const int x, const int y, const int width, const int height) const {
  ClockRender::DateTimeView view;
  bool available = false;
#ifndef SIMULATOR
  HalGPIO::DateTime dateTime;
  available = gpio.readDateTime(dateTime);
  if (available) {
    view.year = dateTime.year;
    view.month = dateTime.month;
    view.day = dateTime.day;
    view.hour = dateTime.hour;
    view.minute = dateTime.minute;
    view.weekday = dateTime.weekday;
  }
#endif
  if (available) {
    renderedKey_ = static_cast<uint64_t>(view.year) * 100000000ULL + static_cast<uint64_t>(view.month) * 1000000ULL +
                   static_cast<uint64_t>(view.day) * 10000ULL + static_cast<uint64_t>(view.hour) * 100ULL + view.minute;
  }
  ClockRender::render(renderer_, SETTINGS.sleepClockStyle, view, available, x, y, width, height);
}

bool Clock::needsRefresh() const {
  uint64_t currentKey = 0;
  return readDateTime(currentKey) && currentKey != renderedKey_;
}

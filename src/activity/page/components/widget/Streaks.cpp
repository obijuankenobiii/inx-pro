#include "Streaks.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "images/StreaksIcon.h"
#include "state/Statistics.h"
#include "system/Fonts.h"

namespace {

constexpr const char* kWeekdays[] = {"M", "T", "W", "T", "F", "S", "S"};

enum class DayState { Active, Missed, Upcoming };

int mondayBasedWeekday(const int64_t day) {
  const int value = static_cast<int>((day + 3) % 7);
  return value < 0 ? value + 7 : value;
}

int daysInMonth(const int year, const int month) {
  if (month == 2) {
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return leap ? 29 : 28;
  }
  return month == 4 || month == 6 || month == 9 || month == 11 ? 30 : 31;
}

uint32_t readingTimeForDate(const std::vector<ReadingHistoryEntry>& history, const uint32_t dateKey) {
  for (const ReadingHistoryEntry& entry : history) {
    if (entry.dateKey == dateKey) return entry.readingTimeMs;
  }
  return 0;
}

int64_t latestReadingDay(const std::vector<ReadingHistoryEntry>& history) {
  int64_t latest = 0;
  for (const ReadingHistoryEntry& entry : history) latest = std::max(latest, readingDateToDay(entry.dateKey));
  return latest;
}

void drawLine(GfxRenderer& renderer, int x1, int y1, const int x2, const int y2, const bool state) {
  const int dx = std::abs(x2 - x1);
  const int sx = x1 < x2 ? 1 : -1;
  const int dy = -std::abs(y2 - y1);
  const int sy = y1 < y2 ? 1 : -1;
  int error = dx + dy;
  while (true) {
    renderer.drawPixel(x1, y1, state);
    if (x1 == x2 && y1 == y2) return;
    const int doubled = 2 * error;
    if (doubled >= dy) {
      error += dy;
      x1 += sx;
    }
    if (doubled <= dx) {
      error += dx;
      y1 += sy;
    }
  }
}

void drawCheck(GfxRenderer& renderer, const int cx, const int cy, const int radius) {
  const int leftX = cx - radius / 2;
  const int centerX = cx - 1;
  const int rightX = cx + radius / 2;
  const int centerY = cy + radius / 3;
  const int endY = cy - radius / 3;
  drawLine(renderer, leftX, cy - 1, centerX, centerY, false);
  drawLine(renderer, leftX, cy, centerX + 1, centerY + 1, false);
  drawLine(renderer, centerX, centerY, rightX, endY, false);
  drawLine(renderer, centerX + 1, centerY + 1, rightX + 1, endY + 1, false);
}

void drawGrayCircle(GfxRenderer& renderer, const int cx, const int cy, const int radius) {
  const int radiusSquared = radius * radius;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (dx * dx + dy * dy <= radiusSquared) {
        renderer.rectangle.fill(cx + dx, cy + dy, 1, 1, static_cast<int>(GfxRenderer::FillTone::Gray), false);
      }
    }
  }
}

}

uint64_t Streaks::dataKey() const {
  const std::vector<ReadingHistoryEntry>& history = getReadingHistory();
  uint32_t today = 0;
  if (!currentRtcDateKey(today)) today = history.empty() ? 0 : readingDayToDate(latestReadingDay(history));

  uint64_t key = today;
  for (const ReadingHistoryEntry& entry : history) {
    key = key * 1315423911ULL + entry.dateKey * 31ULL + entry.readingTimeMs;
  }
  return key;
}

bool Streaks::needsRefresh() const { return dataKey() != renderedKey_; }

void Streaks::render(const int x, const int y, const int width, const int height, const bool monthly) const {
  renderContent(x, y, width, height, false, monthly);
}

void Streaks::preview(const int x, const int y, const int width, const int height, const bool monthly) const {
  renderContent(x, y, width, height, true, monthly);
}

void Streaks::renderContent(const int x, const int y, const int width, const int height, const bool sample,
                            const bool monthly) const {
  if (width <= 0 || height <= 0) return;
  renderer_.rectangle.fill(x, y, width, height, false);

  std::array<DayState, 7> days{};
  const std::vector<ReadingHistoryEntry>& history = getReadingHistory();
  int streak = 0;
  uint64_t currentKey = 0;
  int64_t referenceDay = 0;
  if (sample) {
    days = {DayState::Active, DayState::Active, DayState::Active, DayState::Missed,
            DayState::Active, DayState::Upcoming, DayState::Upcoming};
    streak = 19;
    referenceDay = readingDateToDay(20260615);
  } else {
    uint32_t todayKey = 0;
    if (currentRtcDateKey(todayKey)) {
      referenceDay = readingDateToDay(todayKey);
    } else if (!history.empty()) {
      referenceDay = latestReadingDay(history);
      todayKey = readingDayToDate(referenceDay);
    }
    currentKey = dataKey();
    if (referenceDay == 0) {
      renderer_.text.render(systemFontIdForPointSize(8), x + 16, y + std::max(0, height / 2 - 5),
                            "Streaks unavailable", true, EpdFontFamily::REGULAR);
      renderedKey_ = currentKey;
      return;
    }

    if (!monthly) {
      const int64_t weekStart = referenceDay - mondayBasedWeekday(referenceDay);
      for (size_t index = 0; index < days.size(); ++index) {
        const int64_t day = weekStart + static_cast<int64_t>(index);
        const bool read = readingTimeForDate(history, readingDayToDate(day)) > 0;
        days[index] = read ? DayState::Active : (day < referenceDay ? DayState::Missed : DayState::Upcoming);
      }
    }

    int64_t streakDay = referenceDay;
    if (readingTimeForDate(history, readingDayToDate(streakDay)) == 0) --streakDay;
    while (streakDay >= 0 && readingTimeForDate(history, readingDayToDate(streakDay)) > 0) {
      ++streak;
      --streakDay;
    }
  }

  const int paddingX = std::min(20, std::max(8, width / 20));
  constexpr int paddingY = 20;
  constexpr int sectionGap = 40;
  constexpr int iconWidth = 64;
  constexpr int iconHeight = 65;

  const int labelFont = systemFontIdForPointSize(8);
  const int labelLine = renderer_.text.getLineHeight(labelFont);
  const int valueFont = systemFontIdForPointSize(18);
  constexpr uint8_t valueScale = 150;
  constexpr uint8_t labelScale = 125;
  constexpr int textGap = 10;
  const int valueLine = renderer_.text.getLineHeight(valueFont);
  const int scaledLabelLine = labelLine * labelScale / 100;
  const int topSectionHeight = std::max(iconHeight, 6 + valueLine + scaledLabelLine);
  const int circleRadius = monthly ? std::max(10, std::min(13, height / 16))
                                   : std::max(9, std::min(15, height / 14 + 2));
  const int calendarFont = systemFontIdForPointSize(10);
  const int calendarLine = renderer_.text.getLineHeight(calendarFont);
  int monthFirstWeekday = 0;
  int monthDayCount = 0;
  int monthRowCount = 0;
  int64_t monthStartDay = 0;
  if (monthly) {
    const uint32_t referenceDate = readingDayToDate(referenceDay);
    const int year = static_cast<int>(referenceDate / 10000U);
    const int month = static_cast<int>((referenceDate / 100U) % 100U);
    monthDayCount = daysInMonth(year, month);
    monthStartDay = readingDateToDay(static_cast<uint32_t>(year * 10000 + month * 100 + 1));
    monthFirstWeekday = mondayBasedWeekday(monthStartDay);
    monthRowCount = (monthFirstWeekday + monthDayCount + 6) / 7;
  }
  const int contentHeight = std::max(0, height - paddingY * 2);
  int calendarRowStep = 0;
  if (monthly) {
    const int availableGridHeight = std::max(1, contentHeight - topSectionHeight - 20 - calendarLine - 6);
    calendarRowStep = std::max(circleRadius * 2, availableGridHeight / std::max(1, monthRowCount));
  }
  const int bottomSectionHeight = monthly
                                     ? calendarLine + 6 + monthRowCount * calendarRowStep
                                     : circleRadius * 2 + 8 + labelLine;
  const int availableGap = std::max(0, contentHeight - topSectionHeight - bottomSectionHeight);
  const int actualGap = std::min(monthly ? 20 : sectionGap, availableGap);
  const int contentBlockHeight = topSectionHeight + actualGap + bottomSectionHeight;
  const int blockTop = y + paddingY + std::max(0, (contentHeight - contentBlockHeight) / 2);
  const int top = blockTop;
  const int iconX = x + paddingX;
  const int iconY = top;
  renderer_.bitmap.icon(StreaksIcon, iconX, iconY, iconWidth, iconHeight);

  const int valueX = iconX + iconWidth + 14;
  char streakText[16];
  std::snprintf(streakText, sizeof(streakText), "%d", streak);
  const int countY = top + 6;
  const int labelY = countY + valueLine + textGap;
  renderer_.text.renderScaled(valueFont, valueX, countY, streakText, valueScale, true, EpdFontFamily::BOLD);
  renderer_.text.renderScaledGray(systemFontIdForPointSize(8), valueX, labelY - 10, "Day streak", labelScale, true,
                                  EpdFontFamily::REGULAR);

  const int circlesTop = blockTop + topSectionHeight + actualGap;
  const int availableWidth = std::max(1, width - paddingX * 2);
  if (monthly) {
    for (size_t index = 0; index < 7; ++index) {
      const int cx = x + paddingX + (availableWidth * static_cast<int>(index * 2 + 1)) / 14;
      const int textWidth = renderer_.text.getWidth(calendarFont, kWeekdays[index]);
      renderer_.text.renderGray(calendarFont, cx - textWidth / 2, circlesTop, kWeekdays[index], true,
                                EpdFontFamily::REGULAR);
    }

    const int gridTop = circlesTop + calendarLine + 6;
    const int rowStep = calendarRowStep;
    for (int dayOfMonth = 1; dayOfMonth <= monthDayCount; ++dayOfMonth) {
      const int cell = monthFirstWeekday + dayOfMonth - 1;
      const int column = cell % 7;
      const int row = cell / 7;
      const int cx = x + paddingX + (availableWidth * (column * 2 + 1)) / 14;
      const int cy = gridTop + row * rowStep + circleRadius;
      const int64_t day = monthStartDay + dayOfMonth - 1;
      const bool read = sample
                           ? (dayOfMonth <= 3 || dayOfMonth == 5 || dayOfMonth == 8 || dayOfMonth == 11)
                           : readingTimeForDate(history, readingDayToDate(day)) > 0;
      const DayState state = read ? DayState::Active : (day < referenceDay ? DayState::Missed : DayState::Upcoming);
      char dateText[4];
      std::snprintf(dateText, sizeof(dateText), "%d", dayOfMonth);
      const int dateWidth = renderer_.text.getWidth(calendarFont, dateText);
      switch (state) {
        case DayState::Active:
          renderer_.circle.render(cx, cy, circleRadius, true);
          drawCheck(renderer_, cx, cy, circleRadius);
          break;
        case DayState::Missed:
          drawGrayCircle(renderer_, cx, cy, circleRadius);
          renderer_.text.render(calendarFont, cx - dateWidth / 2, cy - calendarLine / 2, dateText, true,
                                EpdFontFamily::REGULAR);
          break;
        case DayState::Upcoming:
          renderer_.text.renderGray(calendarFont, cx - dateWidth / 2, cy - calendarLine / 2, dateText, true,
                                    EpdFontFamily::REGULAR);
          break;
      }
    }
    if (!sample) renderedKey_ = currentKey;
    return;
  }

  const int circleY = circlesTop + circleRadius;
  const int rowY = circlesTop + circleRadius * 2 + 8;
  for (size_t index = 0; index < days.size(); ++index) {
    const int cx = x + paddingX + (availableWidth * static_cast<int>(index * 2 + 1)) / 14;
    switch (days[index]) {
      case DayState::Active:
        renderer_.circle.render(cx, circleY, circleRadius, true);
        drawCheck(renderer_, cx, circleY, circleRadius);
        break;
      case DayState::Missed:
        drawGrayCircle(renderer_, cx, circleY, circleRadius);
        break;
      case DayState::Upcoming:
        drawGrayCircle(renderer_, cx, circleY, circleRadius);
        renderer_.circle.render(cx, circleY, circleRadius - 2, false);
        break;
    }
    const int labelWidth = renderer_.text.getWidth(labelFont, kWeekdays[index]);
    renderer_.text.renderGray(labelFont, cx - labelWidth / 2, rowY, kWeekdays[index], true,
                              EpdFontFamily::REGULAR);
  }

  if (!sample) renderedKey_ = currentKey;
}

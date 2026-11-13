#include "WeeklyStatistics.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "state/ReadingGoal.h"
#include "state/Statistics.h"
#include "system/Fonts.h"

namespace {

void drawLine(GfxRenderer& renderer, int x1, int y1, const int x2, const int y2) {
  const int dx = std::abs(x2 - x1);
  const int sx = x1 < x2 ? 1 : -1;
  const int dy = -std::abs(y2 - y1);
  const int sy = y1 < y2 ? 1 : -1;
  int error = dx + dy;
  while (true) {
    renderer.drawPixel(x1, y1, true);
    if (x1 == x2 && y1 == y2) break;
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

int mondayBasedWeekday(const int64_t day) {
  const int value = static_cast<int>((day + 3) % 7);
  return value < 0 ? value + 7 : value;
}

uint32_t readingTimeForDate(const std::vector<ReadingHistoryEntry>& history, const uint32_t dateKey) {
  for (const ReadingHistoryEntry& entry : history) {
    if (entry.dateKey == dateKey) return entry.readingTimeMs;
  }
  return 0;
}

std::string formatReadingDuration(const uint32_t readingTimeMs) {
  const uint32_t totalMinutes = (readingTimeMs + 30000UL) / 60000UL;
  const uint32_t hours = totalMinutes / 60U;
  const uint32_t minutes = totalMinutes % 60U;
  char text[24];
  if (hours > 0 && minutes > 0) {
    std::snprintf(text, sizeof(text), "%uh %um", static_cast<unsigned>(hours), static_cast<unsigned>(minutes));
  } else if (hours > 0) {
    std::snprintf(text, sizeof(text), "%uh", static_cast<unsigned>(hours));
  } else {
    std::snprintf(text, sizeof(text), "%um", static_cast<unsigned>(minutes));
  }
  return text;
}

}

void WeeklyStatistics::render(const int x, const int y, const int width, const int height, const bool compact,
                              const bool lineChart, const bool showSummary) const {
  renderContent(x, y, width, height, false, compact, lineChart, showSummary);
}

void WeeklyStatistics::preview(const int x, const int y, const int width, const int height,
                               const bool compact, const bool lineChart, const bool showSummary) const {
  renderContent(x, y, width, height, true, compact, lineChart, showSummary);
}

void WeeklyStatistics::renderContent(const int x, const int y, const int width, const int height,
                                     const bool sample, const bool compact, const bool lineChart,
                                     const bool showSummary) const {
  if (width <= 0 || height <= 0) return;
  renderer_.rectangle.fill(x, y, width, height, false);

  constexpr const char* kWeekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  constexpr int kHorizontalPadding = 20;
  const int verticalPadding = compact ? 5 : 20;
  const int left = x + kHorizontalPadding;
  const int right = x + width - kHorizontalPadding;
  const int headerFont = systemFontIdForPointSize(10);
  const int valueFont = systemFontIdForPointSize(18);
  const int smallFont = systemFontIdForPointSize(8);
  const int headerLine = renderer_.text.getLineHeight(headerFont);
  const int valueLine = renderer_.text.getLineHeight(valueFont);
  const int smallLine = renderer_.text.getLineHeight(smallFont);
  if (right <= left || height < (showSummary ? headerLine + valueLine + smallLine + 50 : smallLine + 50)) return;

  std::array<uint32_t, 7> daily{};
  uint32_t totalMs = 0;
  uint32_t goalMs = 0;
  int64_t weekStart = 0;
  uint32_t today = 0;
  const std::vector<ReadingHistoryEntry>& history = getReadingHistory();
  if (sample) {
    daily = {12U * 60000U, 0, 24U * 60000U, 8U * 60000U, 32U * 60000U, 16U * 60000U, 20U * 60000U};
    for (const uint32_t value : daily) totalMs += value;
    goalMs = 7U * 45U * 60000U;
  } else if (currentRtcDateKey(today)) {
    weekStart = readingDateToDay(today);
    weekStart -= mondayBasedWeekday(weekStart);
    for (size_t index = 0; index < daily.size(); ++index) {
      const uint32_t value = readingTimeForDate(history, readingDayToDate(weekStart + static_cast<int64_t>(index)));
      daily[index] = value;
      totalMs += value;
    }
    const ReadingGoal::Status goal = ReadingGoal::status();
    if (goal.goalMinutes > 0) goalMs = static_cast<uint32_t>(goal.goalMinutes) * 7UL * 60UL * 1000UL;
  } else if (!history.empty()) {
    weekStart = readingDateToDay(history.back().dateKey);
    weekStart -= mondayBasedWeekday(weekStart);
    for (size_t index = 0; index < daily.size(); ++index) {
      const uint32_t value = readingTimeForDate(history, readingDayToDate(weekStart + static_cast<int64_t>(index)));
      daily[index] = value;
      totalMs += value;
    }
    const ReadingGoal::Status goal = ReadingGoal::status();
    if (goal.goalMinutes > 0) goalMs = static_cast<uint32_t>(goal.goalMinutes) * 7UL * 60UL * 1000UL;
  } else {
    renderer_.text.render(headerFont, left, y + verticalPadding, "Statistics unavailable", true,
                          EpdFontFamily::REGULAR);
    return;
  }

  int plotTop = y + verticalPadding + 8;
  if (showSummary) {
    renderer_.text.renderGray(headerFont, left, y + verticalPadding, "This week", true, EpdFontFamily::REGULAR);
    const std::string goalText = "Goal · " + formatReadingDuration(goalMs);
    const int goalWidth = renderer_.text.getWidth(headerFont, goalText.c_str(), EpdFontFamily::REGULAR);
    renderer_.text.renderGray(headerFont, right - goalWidth, y + verticalPadding, goalText.c_str(), true,
                              EpdFontFamily::REGULAR);

    const std::string totalText = formatReadingDuration(totalMs);
    const int totalY = y + verticalPadding + headerLine + 8;
    renderer_.text.render(valueFont, left, totalY, totalText.c_str(), true, EpdFontFamily::BOLD);

    int percent = 0;
    if (goalMs > 0) {
      percent = static_cast<int>((static_cast<uint64_t>(totalMs) * 100ULL + goalMs / 2ULL) / goalMs);
      percent = std::min(100, std::max(0, percent));
    }
    char percentText[12];
    std::snprintf(percentText, sizeof(percentText), "%d%%", percent);
    const int percentWidth = renderer_.text.getWidth(valueFont, percentText, EpdFontFamily::BOLD);
    renderer_.text.render(valueFont, right - percentWidth, totalY, percentText, true, EpdFontFamily::BOLD);
    plotTop = totalY + valueLine + 12;
  }
  const int plotBottom = y + height - verticalPadding - smallLine - 16;
  const int plotHeight = plotBottom - plotTop;
  if (plotHeight <= 12) return;

  uint32_t maximum = goalMs > 0 ? goalMs / 7U : 0;
  int peakIndex = 0;
  for (size_t index = 0; index < daily.size(); ++index) {
    if (daily[index] > maximum) maximum = daily[index];
    if (daily[index] > daily[static_cast<size_t>(peakIndex)]) peakIndex = static_cast<int>(index);
  }
  if (maximum == 0) maximum = 1;

  const int gridLeft = left + 8;
  const int gridRight = right - 8;
  const int gridWidth = std::max(1, gridRight - gridLeft);
  constexpr int barGap = 8;
  const int barWidth = std::max(8, (gridWidth - barGap * 6) / 7);
  for (int level = 0; level <= 4; ++level) {
    const int lineY = plotBottom - (plotHeight * level) / 4;
    renderer_.line.render(gridLeft, lineY, gridRight, lineY, true, LineRender::Style::Dotted);
  }
  if (lineChart) {
    std::array<int, 7> points{};
    for (size_t index = 0; index < daily.size(); ++index) {
      const int pointX = gridLeft + static_cast<int>(index) * (barWidth + barGap) + barWidth / 2;
      const int pointY = plotBottom - static_cast<int>((static_cast<uint64_t>(daily[index]) * plotHeight) / maximum);
      points[index] = pointY;
      if (index > 0) {
        const int previousX = gridLeft + static_cast<int>(index - 1) * (barWidth + barGap) + barWidth / 2;
        drawLine(renderer_, previousX, points[index - 1], pointX, pointY);
      }
    }
    for (size_t index = 0; index < daily.size(); ++index) {
      const int pointX = gridLeft + static_cast<int>(index) * (barWidth + barGap) + barWidth / 2;
      constexpr int pointSize = 9;
      renderer_.rectangle.fill(pointX - pointSize / 2, points[index] - pointSize / 2, pointSize, pointSize,
                               static_cast<int>(GfxRenderer::FillTone::Gray), true);
      const int labelWidth = renderer_.text.getWidth(smallFont, kWeekdays[index]);
      renderer_.text.render(smallFont, pointX - labelWidth / 2, plotBottom + 10, kWeekdays[index], true,
                            EpdFontFamily::REGULAR);
    }
    return;
  }
  for (size_t index = 0; index < daily.size(); ++index) {
    const int barX = gridLeft + static_cast<int>(index) * (barWidth + barGap);
    const uint32_t value = daily[index];
    const int barHeight = value == 0 ? 0 : std::max(4, static_cast<int>((static_cast<uint64_t>(value) * plotHeight) /
                                                                          maximum));
    if (barHeight > 0) {
      const int tone = static_cast<int>(static_cast<int>(index) == peakIndex ? GfxRenderer::FillTone::Ink
                                                                                : GfxRenderer::FillTone::Gray);
      renderer_.rectangle.fill(barX, plotBottom - barHeight, barWidth, barHeight, tone, true);
    }
    const int labelWidth = renderer_.text.getWidth(smallFont, kWeekdays[index]);
    renderer_.text.render(smallFont, barX + (barWidth - labelWidth) / 2, plotBottom + 10, kWeekdays[index], true,
                          static_cast<int>(index) == peakIndex ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }
}

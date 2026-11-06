#include "Heatmap.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

#include "state/Statistics.h"
#include "state/ReadingGoal.h"
#include "system/Fonts.h"

namespace {

struct Cell {
  uint32_t dateKey;
  uint32_t readingTimeMs;
};

uint32_t timeForDate(const uint32_t dateKey, const std::vector<ReadingHistoryEntry>& history) {
  for (const ReadingHistoryEntry& entry : history) {
    if (entry.dateKey == dateKey) return entry.readingTimeMs;
  }
  return 0;
}

int daysInMonth(const int year, const int month) {
  if (month == 2) {
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return leap ? 29 : 28;
  }
  return month == 4 || month == 6 || month == 9 || month == 11 ? 30 : 31;
}

int mondayBasedWeekday(const int64_t day) {
  const int value = static_cast<int>((day + 3) % 7);
  return value < 0 ? value + 7 : value;
}

uint64_t renderKey(const uint32_t today, const std::vector<ReadingHistoryEntry>& history,
                  const HomeTheme::HeatmapView view) {
  const uint32_t latest = history.empty() ? 0 : history.back().readingTimeMs;
  return (static_cast<uint64_t>(today) << 32) ^ (static_cast<uint64_t>(latest) << 1) ^
         static_cast<uint64_t>(history.size() * 3 + static_cast<int>(view));
}

int toneForValue(const uint32_t value, const uint32_t maximum) {
  if (value == 0 || maximum == 0) return static_cast<int>(GfxRenderer::FillTone::Paper);
  if (static_cast<uint64_t>(value) * 3ULL >= static_cast<uint64_t>(maximum) * 2ULL) {
    return static_cast<int>(GfxRenderer::FillTone::Ink);
  }
  return static_cast<int>(GfxRenderer::FillTone::Gray);
}

void drawCell(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
              const int tone) {
  renderer.rectangle.fill(x, y, width, height, tone, true, false);
  renderer.rectangle.render(x, y, width, height, true, true, false);
}

}

void Heatmap::render(const int x, const int y, const int width, const int height,
                     const HomeTheme::HeatmapView view, const bool showLabel,
                     const HomeTheme::CarouselLabelColor labelColor, const bool compact) const {
  renderContent(x, y, width, height, view, false, showLabel, labelColor, compact);
}

void Heatmap::preview(const int x, const int y, const int width, const int height,
                      const HomeTheme::HeatmapView view, const bool showLabel,
                      const HomeTheme::CarouselLabelColor labelColor, const bool compact) const {
  renderContent(x, y, width, height, view, true, showLabel, labelColor, compact);
}

uint32_t Heatmap::hitTestDateKey(const int px, const int py, const int x, const int y, const int width,
                                  const int height, const HomeTheme::HeatmapView view, const bool showLabel,
                                  const bool compact) const {
  if (width <= 0 || height <= 0) return 0;
  uint32_t today = 0;
  if (!currentRtcDateKey(today)) return 0;

  const int titleFont = MONTSERRAT_10_FONT_ID;
  const int smallFont = MONTSERRAT_8_FONT_ID;
  const int titleLine = renderer_.text.getLineHeight(titleFont);
  const int smallLine = renderer_.text.getLineHeight(smallFont);
  constexpr int paddingX = 20;
  constexpr int paddingY = 20;
  constexpr int legendGap = 10;
  const int titleY = y + paddingY;
  const int legendHeight = smallLine;
  const int headerLine = showLabel ? titleLine : 0;
  const int gridTop = titleY + headerLine + 2;
  const int gridBottom = y + height - paddingY - legendHeight - legendGap;
  if (gridBottom <= gridTop) return 0;

  int columns = 14;
  int rows = 1;
  int labelWidth = 0;
  if (view == HomeTheme::HeatmapView::Weekly) {
    columns = 8;
    rows = 7;
    labelWidth = renderer_.text.getWidth(smallFont, "W") + 10;
  } else if (view == HomeTheme::HeatmapView::Monthly) {
    columns = 7;
    rows = 6;
    labelWidth = renderer_.text.getWidth(smallFont, "W") + 10;
  }

  int gridRight = x + width - paddingX;
  int gridLeft = x + paddingX + labelWidth;
  const bool showWeekTotal = view == HomeTheme::HeatmapView::Weekly || view == HomeTheme::HeatmapView::Monthly;
  if (showWeekTotal) {
    const int minimumStatsWidth = compact ? 120 : 150;
    const int statsWidth = minimumStatsWidth;
    const int statsLeft = gridRight - statsWidth;
    gridRight = statsLeft - 20;
  }
  const int gridWidth = std::max(1, gridRight - gridLeft);
  const int gap = width >= 300 ? 4 : 3;
  const int maxCellWidth = std::max(2, (gridWidth - gap * (columns - 1)) / columns);
  const int maxCellHeight = std::max(2, (gridBottom - gridTop - gap * (rows - 1)) / rows);
  const int cellWidth = std::max(2, std::min(maxCellWidth, maxCellHeight));
  const int cellHeight = cellWidth;
  const int actualGridWidth = cellWidth * columns + gap * (columns - 1);
  const int gridX = showWeekTotal ? gridLeft : gridRight - actualGridWidth;
  const int actualGridHeight = cellHeight * rows + gap * (rows - 1);
  const int actualGridTop = gridTop + std::max(0, (gridBottom - gridTop - actualGridHeight) / 2);
  if (px < gridX || py < actualGridTop || px >= gridX + cellWidth * columns + gap * (columns - 1) ||
      py >= actualGridTop + actualGridHeight) {
    return 0;
  }

  const int column = (px - gridX) / (cellWidth + gap);
  const int row = (py - actualGridTop) / (cellHeight + gap);
  if (column < 0 || column >= columns || row < 0 || row >= rows ||
      (px - gridX) % (cellWidth + gap) >= cellWidth ||
      (py - actualGridTop) % (cellHeight + gap) >= cellHeight) {
    return 0;
  }

  const int64_t todayDay = readingDateToDay(today);
  if (view == HomeTheme::HeatmapView::Daily) {
    return readingDayToDate(todayDay - (columns - 1 - column));
  }
  if (view == HomeTheme::HeatmapView::Weekly) {
    const int64_t weekStart = todayDay - mondayBasedWeekday(todayDay);
    const int64_t date = weekStart - (columns - 1 - column) * 7 + row;
    return date <= todayDay ? readingDayToDate(date) : 0;
  }

  const int year = static_cast<int>(today / 10000UL);
  const int month = static_cast<int>((today / 100UL) % 100UL);
  const uint32_t firstDateKey = static_cast<uint32_t>(year) * 10000UL + static_cast<uint32_t>(month) * 100UL + 1U;
  const int monthDay = row * columns + column - mondayBasedWeekday(readingDateToDay(firstDateKey)) + 1;
  if (monthDay < 1 || monthDay > daysInMonth(year, month)) return 0;
  return static_cast<uint32_t>(year) * 10000UL + static_cast<uint32_t>(month) * 100UL +
         static_cast<uint32_t>(monthDay);
}

bool Heatmap::needsRefresh(const HomeTheme::HeatmapView view) const {
  uint32_t today = 0;
  if (!currentRtcDateKey(today)) return false;
  return renderKey(today, getReadingHistory(), view) != renderedKey_;
}

void Heatmap::renderContent(const int x, const int y, const int width, const int height,
                            const HomeTheme::HeatmapView view, const bool sample, const bool showLabel,
                            const HomeTheme::CarouselLabelColor labelColor, const bool compact) const {
  if (width <= 0 || height <= 0) return;
  renderer_.rectangle.fill(x, y, width, height, false);

  uint32_t today = 0;
  const bool rtcAvailable = currentRtcDateKey(today);
  const std::vector<ReadingHistoryEntry>& history = getReadingHistory();
  if (!sample && !rtcAvailable) {
    renderer_.text.render(systemFontId(), x + 12, y + std::max(0, height / 2 - 5), "Heatmap unavailable", true,
                          EpdFontFamily::BOLD);
    return;
  }
  if (sample) today = 20260831;

  const int titleFont = MONTSERRAT_10_FONT_ID;
  const int smallFont = MONTSERRAT_8_FONT_ID;
  const int titleLine = renderer_.text.getLineHeight(titleFont);
  const int smallLine = renderer_.text.getLineHeight(smallFont);
  const int paddingX = 20;
  constexpr int paddingY = 20;
  const int titleY = y + paddingY;
  if (showLabel) {
    if (labelColor == HomeTheme::CarouselLabelColor::Gray) {
      renderer_.text.renderGray(titleFont, x + paddingX, titleY, "Reading heatmap", true, EpdFontFamily::BOLD);
    } else {
      renderer_.text.render(titleFont, x + paddingX, titleY, "Reading heatmap", true, EpdFontFamily::BOLD);
    }
  }
  const int legendHeight = smallLine;
  constexpr int legendGap = 10;
  const int headerLine = showLabel ? titleLine : 0;
  const int gridTop = titleY + headerLine + 2;
  const int gridBottom = y + height - paddingY - legendHeight - legendGap;
  if (gridBottom <= gridTop) return;

  int columns = 14;
  int rows = 1;
  int labelWidth = 0;
  if (view == HomeTheme::HeatmapView::Weekly) {
    columns = 8;
    rows = 7;
    labelWidth = renderer_.text.getWidth(smallFont, "W") + 10;
  } else if (view == HomeTheme::HeatmapView::Monthly) {
    columns = 7;
    rows = 6;
    labelWidth = renderer_.text.getWidth(smallFont, "W") + 10;
  }

  int gridRight = x + width - paddingX;
  int gridLeft = x + paddingX + labelWidth;
  const bool showWeekTotal = view == HomeTheme::HeatmapView::Weekly || view == HomeTheme::HeatmapView::Monthly;
  const int minimumStatsWidth = compact ? 120 : 150;
  const int weeklyStatsWidth = showWeekTotal ? minimumStatsWidth : 0;
  if (weeklyStatsWidth > 0) {
    const int statsLeft = gridRight - weeklyStatsWidth;
    gridRight = statsLeft - 20;
  }
  const int gridWidth = std::max(1, gridRight - gridLeft);
  const int gap = width >= 300 ? 4 : 3;
  const int maxCellWidth = std::max(2, (gridWidth - gap * (columns - 1)) / columns);
  const int maxCellHeight = std::max(2, (gridBottom - gridTop - gap * (rows - 1)) / rows);
  const int naturalCellWidth = std::min(maxCellWidth, maxCellHeight);
  const int cellWidth = std::max(2, naturalCellWidth);
  const int cellHeight = cellWidth;
  const int actualGridWidth = cellWidth * columns + gap * (columns - 1);
  const int gridX = showWeekTotal ? gridLeft : gridRight - actualGridWidth;
  const int actualGridHeight = cellHeight * rows + gap * (rows - 1);
  const int actualGridTop = gridTop + std::max(0, (gridBottom - gridTop - actualGridHeight) / 2);

  std::array<Cell, 56> cells{};
  uint32_t maximum = 0;
  if (view == HomeTheme::HeatmapView::Daily) {
    for (int i = 0; i < columns; ++i) {
      const int64_t date = readingDateToDay(today) - (columns - 1 - i);
      cells[static_cast<size_t>(i)] = {readingDayToDate(date), 0};
      cells[static_cast<size_t>(i)].readingTimeMs =
          sample ? (i == columns - 2 ? 18U * 60000U : (i % 4 == 0 ? 6U * 60000U : 0U))
                 : timeForDate(cells[static_cast<size_t>(i)].dateKey, history);
      maximum = std::max(maximum, cells[static_cast<size_t>(i)].readingTimeMs);
    }
  } else if (view == HomeTheme::HeatmapView::Weekly) {
    const int64_t weekStart = readingDateToDay(today) - mondayBasedWeekday(readingDateToDay(today));
    for (int column = 0; column < columns; ++column) {
      for (int row = 0; row < rows; ++row) {
        const int index = row * columns + column;
        const int64_t date = weekStart - (columns - 1 - column) * 7 + row;
        cells[static_cast<size_t>(index)] = {date <= readingDateToDay(today) ? readingDayToDate(date) : 0, 0};
        cells[static_cast<size_t>(index)].readingTimeMs =
            sample ? ((column + row) % 5 == 0 ? 14U * 60000U : 0U)
                   : timeForDate(cells[static_cast<size_t>(index)].dateKey, history);
        maximum = std::max(maximum, cells[static_cast<size_t>(index)].readingTimeMs);
      }
    }
  } else {
    const int64_t todayDay = readingDateToDay(today);
    const int year = static_cast<int>(today / 10000UL);
    const int month = static_cast<int>((today / 100UL) % 100UL);
    const uint32_t firstDateKey = static_cast<uint32_t>(year) * 10000UL + static_cast<uint32_t>(month) * 100UL + 1U;
    const int firstWeekday = mondayBasedWeekday(readingDateToDay(firstDateKey));
    const int monthDays = daysInMonth(year, month);
    for (int row = 0; row < rows; ++row) {
      for (int column = 0; column < columns; ++column) {
        const int index = row * columns + column;
        const int monthDay = index - firstWeekday + 1;
        if (monthDay < 1 || monthDay > monthDays) {
          cells[static_cast<size_t>(index)] = {0, 0};
        } else {
          const uint32_t dateKey = static_cast<uint32_t>(year) * 10000UL + static_cast<uint32_t>(month) * 100UL +
                                    static_cast<uint32_t>(monthDay);
          cells[static_cast<size_t>(index)] = {dateKey, 0};
          cells[static_cast<size_t>(index)].readingTimeMs =
              sample ? ((row == 2 && column == 3) || (row == 4 && column == 5) ? 20U * 60000U : 0U)
                     : timeForDate(dateKey, history);
        }
        maximum = std::max(maximum, cells[static_cast<size_t>(index)].readingTimeMs);
      }
    }
  }

  if (showWeekTotal) {
    uint64_t totalWeekMs = 0;
    const int64_t todayDay = readingDateToDay(today);
    const int64_t weekStart = todayDay - mondayBasedWeekday(todayDay);
    for (int row = 0; row < 7; ++row) {
      const int64_t date = weekStart + row;
      if (date > todayDay) continue;
      if (sample) {
        totalWeekMs += ((7 + row) % 5 == 0 ? 14U * 60000U : 0U);
      } else {
        totalWeekMs += timeForDate(readingDayToDate(date), history);
      }
    }

    const ReadingGoal::Status goalStatus = ReadingGoal::status();
    const uint64_t goalWeekMs = static_cast<uint64_t>(goalStatus.goalMinutes) * 7ULL * 60ULL * 1000ULL;
    char goalHoursText[20];
    if (goalWeekMs > 0) {
      std::snprintf(goalHoursText, sizeof(goalHoursText), "%.1fh",
                    static_cast<double>(goalWeekMs) / 3600000.0);
    } else {
      std::snprintf(goalHoursText, sizeof(goalHoursText), "Off");
    }
    char totalHoursText[20];
    const double totalHours = static_cast<double>(totalWeekMs) / 3600000.0;
    std::snprintf(totalHoursText, sizeof(totalHoursText), "%.1fh", totalHours);

    const int statsRight = x + width - paddingX;
    const int statsLeft = gridX + actualGridWidth + 20;
    const int valueFont = MONTSERRAT_18_FONT_ID;
    const int valueLine = renderer_.text.getLineHeight(valueFont);
    constexpr int barHeight = 10;
    const int barWidth = std::max(1, statsRight - statsLeft);
    int barY = gridBottom - barHeight - 4;

    if (compact) {
      const int groupHeight = smallLine + 4 + valueLine;
      const int summaryBottom = gridBottom - barHeight - 8;
      const int groupGap = std::max(10, (summaryBottom - gridTop - groupHeight * 2) / 3);
      const int goalLabelY = gridTop + groupGap;
      const int goalValueY = goalLabelY + smallLine + 4;
      const int weekLabelY = goalValueY + valueLine + groupGap;
      const int weekValueY = weekLabelY + smallLine + 4;
      renderer_.text.renderGray(smallFont, statsLeft, goalLabelY, "Goal", true, EpdFontFamily::REGULAR);
      renderer_.text.renderGray(valueFont, statsLeft, goalValueY, goalHoursText, true, EpdFontFamily::REGULAR);
      renderer_.text.render(smallFont, statsLeft, weekLabelY, "This week", true, EpdFontFamily::REGULAR);
      renderer_.text.render(valueFont, statsLeft, weekValueY, totalHoursText, true, EpdFontFamily::REGULAR);
    } else {
      const int goalLabelWidth = renderer_.text.getWidth(smallFont, "Goal");
      const int weekLabelWidth = renderer_.text.getWidth(smallFont, "This week");
      const int goalHoursWidth = renderer_.text.getWidth(valueFont, goalHoursText, EpdFontFamily::BOLD);
      const int statsY = gridTop + 4;
      renderer_.text.render(smallFont, statsLeft, statsY, "This week", true, EpdFontFamily::BOLD);
      renderer_.text.renderGray(smallFont, statsRight - goalLabelWidth, statsY, "Goal", true, EpdFontFamily::BOLD);
      renderer_.text.render(valueFont, statsLeft, statsY + smallLine + 8, totalHoursText, true,
                            EpdFontFamily::BOLD);
      renderer_.text.renderGray(valueFont, statsRight - goalHoursWidth, statsY + smallLine + 8,
                                goalHoursText, true, EpdFontFamily::BOLD);
    }

    renderer_.rectangle.fill(statsLeft, barY, barWidth, barHeight,
                             static_cast<int>(GfxRenderer::FillTone::Paper), true);
    if (goalWeekMs > 0) {
      renderer_.rectangle.fill(statsLeft, barY, barWidth, barHeight,
                               static_cast<int>(GfxRenderer::FillTone::Gray), true);
      const int actualWidth = static_cast<int>(std::min<uint64_t>(barWidth,
          (static_cast<uint64_t>(barWidth) * totalWeekMs + goalWeekMs / 2ULL) / goalWeekMs));
      if (actualWidth > 0) {
        renderer_.rectangle.fill(statsLeft, barY, actualWidth, barHeight,
                                 static_cast<int>(GfxRenderer::FillTone::Ink), true);
      }
    }
  }

  if (view != HomeTheme::HeatmapView::Daily) {
    static constexpr const char* const weekdayLabels[] = {"M", "T", "W", "T", "F", "S", "S"};
    const int weekdayLabelWidth = renderer_.text.getWidth(smallFont, "W");
    const int weekdayLabelX = gridX - weekdayLabelWidth - 6;
    for (int row = 0; row < rows; ++row) {
      const int labelY = actualGridTop + row * (cellHeight + gap) + std::max(0, (cellHeight - smallLine) / 2);
      renderer_.text.render(smallFont, weekdayLabelX, labelY, weekdayLabels[row], true);
    }
  }

  for (int row = 0; row < rows; ++row) {
    for (int column = 0; column < columns; ++column) {
      const int index = row * columns + column;
      const int cellX = gridX + column * (cellWidth + gap);
      const int cellY = actualGridTop + row * (cellHeight + gap);
      drawCell(renderer_, cellX, cellY, cellWidth, cellHeight,
               toneForValue(cells[static_cast<size_t>(index)].readingTimeMs, maximum));
    }
  }

  const int legendY = y + height - paddingY - smallLine;
  const int square = std::max(4, std::min(10, smallLine - 1));
  const int squareY = legendY + std::max(0, (smallLine - square) / 2);
  const int lessWidth = renderer_.text.getWidth(smallFont, "Less");
  int legendX = x + paddingX;
  renderer_.text.render(smallFont, legendX, legendY, "Less", true);
  legendX += lessWidth + 5;
  drawCell(renderer_, legendX, squareY, square, square, static_cast<int>(GfxRenderer::FillTone::Paper));
  legendX += square + 3;
  drawCell(renderer_, legendX, squareY, square, square, static_cast<int>(GfxRenderer::FillTone::Gray));
  legendX += square + 3;
  drawCell(renderer_, legendX, squareY, square, square, static_cast<int>(GfxRenderer::FillTone::Ink));
  legendX += square + 5;
  renderer_.text.render(smallFont, legendX, legendY, "More", true);

  renderedKey_ = sample ? 1 : renderKey(today, history, view);
}

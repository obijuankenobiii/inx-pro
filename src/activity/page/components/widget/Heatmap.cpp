#include "Heatmap.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include "state/Statistics.h"
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
  renderer.rectangle.fill(x, y, width, height, tone, false);
  renderer.rectangle.render(x, y, width, height, true);
}

}  // namespace

void Heatmap::render(const int x, const int y, const int width, const int height,
                     const HomeTheme::HeatmapView view, const bool showLabel,
                     const HomeTheme::CarouselLabelColor labelColor) const {
  renderContent(x, y, width, height, view, false, showLabel, labelColor);
}

void Heatmap::preview(const int x, const int y, const int width, const int height,
                      const HomeTheme::HeatmapView view, const bool showLabel,
                      const HomeTheme::CarouselLabelColor labelColor) const {
  renderContent(x, y, width, height, view, true, showLabel, labelColor);
}

bool Heatmap::needsRefresh(const HomeTheme::HeatmapView view) const {
  uint32_t today = 0;
  if (!currentRtcDateKey(today)) return false;
  return renderKey(today, getReadingHistory(), view) != renderedKey_;
}

void Heatmap::renderContent(const int x, const int y, const int width, const int height,
                            const HomeTheme::HeatmapView view, const bool sample, const bool showLabel,
                            const HomeTheme::CarouselLabelColor labelColor) const {
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
  const int paddingY = std::max(8, std::min(14, width / 24));
  const int titleY = y + paddingY;
  const char* viewLabel = HomeTheme::heatmapViewLabel(view);
  const int viewWidth = renderer_.text.getWidth(smallFont, viewLabel);
  if (showLabel) {
    if (labelColor == HomeTheme::CarouselLabelColor::Gray) {
      renderer_.text.renderGray(titleFont, x + paddingX, titleY, "Reading heatmap", true, EpdFontFamily::BOLD);
    } else {
      renderer_.text.render(titleFont, x + paddingX, titleY, "Reading heatmap", true, EpdFontFamily::BOLD);
    }
  }
  renderer_.text.render(smallFont, x + width - paddingX - viewWidth, titleY + 2, viewLabel, true,
                        EpdFontFamily::REGULAR);

  const int legendHeight = smallLine + 4;
  // The view label is always visible. Keep a header row for it even when the
  // optional "Reading heatmap" title is hidden, otherwise the grid covers it.
  const int headerLine = showLabel ? titleLine : smallLine + 2;
  const int gridTop = titleY + headerLine + 7;
  const int gridBottom = y + height - paddingY - legendHeight;
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

  const int gridX = x + paddingX + labelWidth;
  const int gridWidth = std::max(1, width - paddingX * 2 - labelWidth);
  const int gap = width >= 300 ? 3 : 2;
  const int cellWidth = std::max(2, (gridWidth - gap * (columns - 1)) / columns);
  const int cellHeight = std::max(2, (gridBottom - gridTop - gap * (rows - 1)) / rows);
  const int actualGridHeight = cellHeight * rows + gap * (rows - 1);
  const int actualGridTop = gridTop + std::max(0, (gridBottom - gridTop - actualGridHeight) / 2);

  // Weekly view is eight weeks by seven days. Keep storage for the largest
  // view; the previous 42-cell array overflowed when the heatmap was selected.
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

  if (view != HomeTheme::HeatmapView::Daily) {
    static constexpr const char* const weekdayLabels[] = {"M", "T", "W", "T", "F", "S", "S"};
    for (int row = 0; row < rows; ++row) {
      const int labelY = actualGridTop + row * (cellHeight + gap) + std::max(0, (cellHeight - smallLine) / 2);
      renderer_.text.render(smallFont, x + paddingX, labelY, weekdayLabels[row], true);
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
  renderer_.text.render(smallFont, x + paddingX, legendY, "Less", true);
  const int square = std::max(4, std::min(10, smallLine - 1));
  const int squareY = legendY + std::max(0, (smallLine - square) / 2);
  int legendX = x + paddingX + renderer_.text.getWidth(smallFont, "Less") + 5;
  drawCell(renderer_, legendX, squareY, square, square, static_cast<int>(GfxRenderer::FillTone::Paper));
  legendX += square + 3;
  drawCell(renderer_, legendX, squareY, square, square, static_cast<int>(GfxRenderer::FillTone::Gray));
  legendX += square + 3;
  drawCell(renderer_, legendX, squareY, square, square, static_cast<int>(GfxRenderer::FillTone::Ink));
  legendX += square + 5;
  renderer_.text.render(smallFont, legendX, legendY, "More", true);

  renderedKey_ = sample ? 1 : renderKey(today, history, view);
}

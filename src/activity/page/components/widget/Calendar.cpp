#include "Calendar.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>

#include <algorithm>
#include <cstdio>

#include "../../navigation/Menu.h"
#include "system/Fonts.h"

extern HalGPIO gpio;

namespace {

constexpr const char* kMonths[] = {"",       "January", "February", "March",    "April",  "May",      "June",
                                   "July",   "August",  "September", "October",  "November", "December"};
constexpr const char* kWeekdays[] = {"M", "T", "W", "T", "F", "S", "S"};

int daysInMonth(const int year, const int month) {
  if (month == 2) {
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return leap ? 29 : 28;
  }
  return (month == 4 || month == 6 || month == 9 || month == 11) ? 30 : 31;
}

int firstWeekday(const int day, const int weekday) {
  return ((weekday - 1 - ((day - 1) % 7) + 700) % 7);
}

void centerInCell(GfxRenderer& renderer, const int font, const int x, const int width, const int y,
                 const char* text, const bool black = true) {
  const int textWidth = renderer.text.getWidth(font, text);
  renderer.text.render(font, x + (width - textWidth) / 2, y, text, black);
}

}

bool Calendar::readDate(int& year, int& month, int& day, int& weekday) const {
#ifdef SIMULATOR
  (void)year;
  (void)month;
  (void)day;
  (void)weekday;
  return false;
#else
  HalGPIO::DateTime dateTime;
  if (!gpio.readDateTime(dateTime) || dateTime.month < 1 || dateTime.month > 12 || dateTime.day < 1 ||
      dateTime.weekday < 1 || dateTime.weekday > 7)
    return false;
  year = dateTime.year;
  month = dateTime.month;
  day = dateTime.day;
  weekday = dateTime.weekday;
  return true;
#endif
}

void Calendar::render(const int x, const int y, const int width, const int height) const {
  int year = 0;
  int month = 0;
  int day = 0;
  int weekday = 0;
  if (!readDate(year, month, day, weekday)) {
    renderer_.text.render(systemFontId(), x + 16, y + std::max(0, height / 2 - 10), "Calendar unavailable", true,
                          EpdFontFamily::BOLD);
    return;
  }

  renderedDateKey_ = year * 10000 + month * 100 + day;
  const int font = MONTSERRAT_10_FONT_ID;
  const int headerFont = MONTSERRAT_14_FONT_ID;
  renderer_.rectangle.fill(x, y, width, height, false);

  const int paddingX = 0;
  const bool isBottomRow = y > navigation::Menu::height;
  const int paddingTop = (height < 300 ? 6 : 5) + (isBottomRow ? 10 : 0);
  const int paddingBottom = height < 300 ? 6 : 5;
  const int contentX = x + paddingX;
  const int contentWidth = std::max(1, width - paddingX * 2);
  const int gridX = contentX;
  const int columnGap = width < 260 ? 3 : 6;
  const int cellWidth = std::max(1, (contentWidth - columnGap * 6) / 7);
  const int headerHeight = renderer_.text.getLineHeight(headerFont);
  const int headerY = y + paddingTop;
  char header[32];
  std::snprintf(header, sizeof(header), "%s %d", month >= 1 && month <= 12 ? kMonths[month] : "Calendar", year);
  const int headerX = gridX + 20;
  renderer_.text.render(headerFont, headerX, headerY, header, true, EpdFontFamily::BOLD);

  const int gridY = headerY + headerHeight + (width < 260 ? 6 : 10);
  const int lineHeight = renderer_.text.getLineHeight(font) + 3;
  const int datesY = gridY + lineHeight + (width < 260 ? 6 : 10);
  const int datesBottom = y + height - paddingBottom;
  const int datesHeight = std::max(1, datesBottom - datesY);
  int rowGap = height < 300 ? 2 : 5;
  if (datesHeight < lineHeight * 6 + rowGap * 5) {
    rowGap = std::max(0, (datesHeight - lineHeight * 6) / 5);
  }
  const int cellHeight = std::max(1, (datesHeight - rowGap * 5) / 6);

  for (int column = 0; column < 7; ++column) {
    centerInCell(renderer_, font, gridX + column * (cellWidth + columnGap), cellWidth, gridY, kWeekdays[column]);
  }

  const int first = firstWeekday(day, weekday);
  for (int number = 1; number <= daysInMonth(year, month); ++number) {
    const int position = first + number - 1;
    const int row = position / 7;
    const int column = position % 7;
    const int cellX = gridX + column * (cellWidth + columnGap);
    const int cellY = datesY + row * (cellHeight + rowGap);
    char dayText[3];
    std::snprintf(dayText, sizeof(dayText), "%d", number);
    const int textY = cellY + (cellHeight - lineHeight) / 2;
    const int textWidth = renderer_.text.getWidth(font, dayText);
    const int textX = cellX + (cellWidth - textWidth) / 2;
    if (number == day) {
      const int circleDiameter =
          std::min(cellWidth + columnGap - 1, cellHeight + rowGap - 1) + 5;
      const int radius = std::max(2, circleDiameter / 2);
      renderer_.circle.render(cellX + cellWidth / 2, cellY + cellHeight / 2, radius, true);
      renderer_.text.render(font, textX, textY, dayText, false);
    } else {
      renderer_.text.render(font, textX, textY, dayText, true);
    }
  }
}

bool Calendar::needsRefresh() const {
  int year = 0;
  int month = 0;
  int day = 0;
  int weekday = 0;
  if (!readDate(year, month, day, weekday)) return false;
  return year * 10000 + month * 100 + day != renderedDateKey_;
}

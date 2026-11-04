#include "ClockRender.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "../../src/state/SystemSetting.h"

namespace {

constexpr int MONTSERRAT_8_FONT_ID = 2501;
constexpr int MONTSERRAT_10_FONT_ID = 2502;
constexpr int MONTSERRAT_12_FONT_ID = 2503;
constexpr int MONTSERRAT_14_FONT_ID = 2504;
constexpr int MONTSERRAT_18_FONT_ID = 2506;
constexpr int MONTSERRAT_CLOCK_70_FONT_ID = 4001;

constexpr const char* WEEKDAYS_SHORT[] = {"", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
constexpr const char* MONTHS_SHORT[] = {"",    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

constexpr int TIME_FONT = MONTSERRAT_CLOCK_70_FONT_ID;
constexpr int LARGE_SYSTEM_FONT = MONTSERRAT_18_FONT_ID;
constexpr int LABEL_LARGE_FONT = MONTSERRAT_18_FONT_ID;
constexpr int TINY_FONT = MONTSERRAT_8_FONT_ID;
constexpr int CLOCK_VISIBLE_HEIGHT = 106;
constexpr int CLOCK_GLYPH_TOP = 104;
constexpr uint8_t COMPACT_TIME_SCALE = 80;

int systemFontId() {
  switch (SETTINGS.systemTextSize) {
    case SystemSetting::SYSTEM_TEXT_MEDIUM:
      return MONTSERRAT_12_FONT_ID;
    case SystemSetting::SYSTEM_TEXT_LARGE:
      return MONTSERRAT_14_FONT_ID;
    case SystemSetting::SYSTEM_TEXT_SMALL:
    default:
      return MONTSERRAT_10_FONT_ID;
  }
}

const char* weekdayShort(uint8_t weekday) { return weekday >= 1 && weekday <= 7 ? WEEKDAYS_SHORT[weekday] : ""; }
const char* monthShort(uint8_t month) { return month >= 1 && month <= 12 ? MONTHS_SHORT[month] : ""; }

int textW(GfxRenderer& renderer, int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  return renderer.text.getWidth(fontId, text, style);
}

int clampInt(int value, int low, int high) {
  if (high < low) {
    return low;
  }
  return std::max(low, std::min(value, high));
}

uint8_t displayHour(const ClockRender::DateTimeView& dt) {
  if (SETTINGS.sleepClockTimeFormat == SystemSetting::CLOCK_24_HOUR) {
    return dt.hour;
  }
  const uint8_t h = dt.hour % 12;
  return h == 0 ? 12 : h;
}

const char* meridiem(const ClockRender::DateTimeView& dt) { return dt.hour >= 12 ? "PM" : "AM"; }

void formatTime(const ClockRender::DateTimeView& dt, char* out, size_t outSize) {
  std::snprintf(out, outSize, "%02u:%02u", displayHour(dt), dt.minute);
}

void formatDateFull(const ClockRender::DateTimeView& dt, char* out, size_t outSize) {
  std::snprintf(out, outSize, "%02u %s, %04u", dt.day, monthShort(dt.month), dt.year);
}

void centerText(GfxRenderer& renderer, int fontId, int x, int w, int y, const char* text, bool black = true,
                EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (w <= 0 || !text || *text == '\0') {
    return;
  }
  const int tx = x + (w - textW(renderer, fontId, text, style)) / 2;
  renderer.text.render(fontId, std::max(0, tx), std::max(0, y), text, black, style);
}

int clockVisibleHeight(const bool compact) { return compact ? (CLOCK_VISIBLE_HEIGHT * COMPACT_TIME_SCALE) / 100 : CLOCK_VISIBLE_HEIGHT; }

int timeTextW(GfxRenderer& renderer, const char* text, const EpdFontFamily::Style style, const bool compact) {
  return compact ? renderer.text.getScaledWidth(TIME_FONT, text, COMPACT_TIME_SCALE, style)
                 : textW(renderer, TIME_FONT, text, style);
}

int clockRenderY(GfxRenderer& renderer, const int visibleTop, const bool compact) {
  if (!compact) return std::max(0, visibleTop - (renderer.text.getFontAscenderSize(TIME_FONT) - CLOCK_GLYPH_TOP));
  const int ascender = renderer.text.getFontAscenderSize(TIME_FONT);
  const int glyphTop = ascender - renderer.text.getGlyphTopInset(TIME_FONT, '8', EpdFontFamily::BOLD);
  const int scaledGlyphTop = (glyphTop * COMPACT_TIME_SCALE + 50) / 100;
  return std::max(0, visibleTop - ascender + scaledGlyphTop);
}

void centerClockLine(GfxRenderer& renderer, const int x, const int w, const int visibleTop, const char* text,
                     const bool black, const bool compact) {
  if (compact) {
    const int scaledWidth = timeTextW(renderer, text, EpdFontFamily::BOLD, true);
    renderer.text.renderScaled(TIME_FONT, x + (w - scaledWidth) / 2, clockRenderY(renderer, visibleTop, true), text,
                               COMPACT_TIME_SCALE, black, EpdFontFamily::BOLD);
    return;
  }
  centerText(renderer, TIME_FONT, x, w, clockRenderY(renderer, visibleTop, false), text, black,
             EpdFontFamily::BOLD);
}

void renderClockLine(GfxRenderer& renderer, const int x, const int visibleTop, const char* text, const bool compact) {
  if (compact) {
    renderer.text.renderScaled(TIME_FONT, std::max(0, x), clockRenderY(renderer, visibleTop, true), text,
                               COMPACT_TIME_SCALE, true, EpdFontFamily::BOLD);
    return;
  }
  renderer.text.render(TIME_FONT, std::max(0, x), clockRenderY(renderer, visibleTop, false), text, true,
                       EpdFontFamily::BOLD);
}

void renderClockSegment(GfxRenderer& renderer, const int x, const int visibleTop, const char* text, const bool black,
                        const EpdFontFamily::Style style, const bool compact) {
  if (compact) {
    renderer.text.renderScaled(TIME_FONT, std::max(0, x), clockRenderY(renderer, visibleTop, true), text,
                               COMPACT_TIME_SCALE, black, style);
    return;
  }
  renderer.text.render(TIME_FONT, std::max(0, x), clockRenderY(renderer, visibleTop, false), text, black, style);
}

void renderUnavailable(GfxRenderer& renderer, int x, int y, int w, int h) {
  const int centerY = y + h / 2;
  centerText(renderer, systemFontId(), x, w, centerY - 28, "Clock unavailable", true, EpdFontFamily::BOLD);
  centerText(renderer, TINY_FONT, x, w, centerY + 8, "Sync time in settings");
}

void renderCenteredDate(GfxRenderer& renderer, const ClockRender::DateTimeView& dt, int x, int y, int w, int h,
                       bool compact);

void renderStackedCity(GfxRenderer& renderer, const ClockRender::DateTimeView& dt, int x, int y, int w, int h,
                       const bool compact) {
  char hourText[4];
  char minuteText[4];
  char dateText[16];
  std::snprintf(hourText, sizeof(hourText), "%02u", displayHour(dt));
  std::snprintf(minuteText, sizeof(minuteText), "%02u", dt.minute);
  std::snprintf(dateText, sizeof(dateText), "%02u %s", dt.day, monthShort(dt.month));

  const int left = x + 20;
  const int visibleHeight = clockVisibleHeight(compact);
  const int stackedHeight = visibleHeight * 2 + 10;
  const int top = y + (h - stackedHeight) / 2;
  renderClockLine(renderer, left, top, hourText, compact);
  renderClockLine(renderer, left + 10, top + visibleHeight + 10, minuteText, compact);

  const int infoX = left + timeTextW(renderer, "88", EpdFontFamily::BOLD, compact) + 16;
  renderer.text.renderGray(LARGE_SYSTEM_FONT, infoX, std::max(0, top + 18), weekdayShort(dt.weekday), true,
                           EpdFontFamily::BOLD);
  renderer.text.render(LARGE_SYSTEM_FONT, infoX, std::max(0, top + 58), dateText, true);
  if (SETTINGS.sleepClockTimeFormat == SystemSetting::CLOCK_12_HOUR) {
    renderer.text.renderGray(LABEL_LARGE_FONT, infoX, std::max(0, top + 120), meridiem(dt), true,
                             EpdFontFamily::BOLD);
  }
}

void renderCenteredDate(GfxRenderer& renderer, const ClockRender::DateTimeView& dt, int x, int y, int w, int h,
                        const bool compact) {
  char timeText[8];
  char hourText[4];
  char minuteText[4];
  char dateText[24];
  formatTime(dt, timeText, sizeof(timeText));
  std::snprintf(hourText, sizeof(hourText), "%02u", displayHour(dt));
  std::snprintf(minuteText, sizeof(minuteText), "%02u", dt.minute);
  formatDateFull(dt, dateText, sizeof(dateText));

  const int hourW = timeTextW(renderer, hourText, EpdFontFamily::BOLD, compact);
  const int colonW = timeTextW(renderer, ":", EpdFontFamily::REGULAR, compact);
  const int minuteW = timeTextW(renderer, minuteText, EpdFontFamily::REGULAR, compact);
  const int timeW = hourW + colonW + minuteW;
  if (timeW <= w - 24) {
    const int visibleHeight = clockVisibleHeight(compact);
    const int timeTop = y + (h - visibleHeight) / 2 - (compact ? 14 : 18);
    const int timeX = x + (w - timeW) / 2 - (compact ? 16 : 20);
    renderClockSegment(renderer, timeX, timeTop, hourText, true, EpdFontFamily::BOLD, compact);
    renderClockSegment(renderer, timeX + hourW, timeTop, ":", true, EpdFontFamily::REGULAR, compact);
    if (compact) {
      renderer.text.renderScaled(TIME_FONT, std::max(0, timeX + hourW + colonW), clockRenderY(renderer, timeTop, true),
                                 minuteText, COMPACT_TIME_SCALE, true, EpdFontFamily::REGULAR);
    } else {
      renderer.text.renderGray(TIME_FONT, std::max(0, timeX + hourW + colonW), clockRenderY(renderer, timeTop, false),
                               minuteText, true, EpdFontFamily::REGULAR);
    }
    if (SETTINGS.sleepClockTimeFormat == SystemSetting::CLOCK_12_HOUR) {
      renderer.text.render(systemFontId(), x + (w + timeW) / 2 - 10, timeTop + 24, meridiem(dt), true,
                           EpdFontFamily::BOLD);
    }
    centerText(renderer, systemFontId(), x, w, timeTop + CLOCK_VISIBLE_HEIGHT + 20, dateText, true);
    return;
  }

  const int gap = 6;
  const int visibleHeight = clockVisibleHeight(compact);
  const int stackTop = y + (h - (visibleHeight * 2 + gap)) / 2 - 10;
  centerClockLine(renderer, x, w, stackTop, hourText, true, compact);
  {
    const int minuteY = clockRenderY(renderer, stackTop + visibleHeight + gap, compact);
    const int minuteX = x + (w - timeTextW(renderer, minuteText, EpdFontFamily::BOLD, compact)) / 2;
    if (compact) {
      renderer.text.renderScaled(TIME_FONT, std::max(0, minuteX), std::max(0, minuteY), minuteText,
                                 COMPACT_TIME_SCALE, true, EpdFontFamily::BOLD);
    } else {
      renderer.text.renderGray(TIME_FONT, std::max(0, minuteX), std::max(0, minuteY), minuteText, true,
                               EpdFontFamily::BOLD);
    }
  }
  if (SETTINGS.sleepClockTimeFormat == SystemSetting::CLOCK_12_HOUR) {
    centerText(renderer, systemFontId(), x, w, stackTop + CLOCK_VISIBLE_HEIGHT * 2 + gap + 6, meridiem(dt), true,
               EpdFontFamily::BOLD);
  }
  centerText(renderer, systemFontId(), x, w, y + h - 42, dateText, true);
}

void renderHorizontalCard(GfxRenderer& renderer, const ClockRender::DateTimeView& dt, int x, int y, int w, int h,
                          const bool compact) {
  if (w < 80 || h < 90) {
    renderCenteredDate(renderer, dt, x, y, w, h, compact);
    return;
  }

  char dayText[4];
  char monthText[8];
  char hourText[4];
  char minuteText[4];
  std::snprintf(dayText, sizeof(dayText), "%02u", dt.day);
  std::snprintf(monthText, sizeof(monthText), "%s", monthShort(dt.month));
  std::snprintf(hourText, sizeof(hourText), "%02u", displayHour(dt));
  std::snprintf(minuteText, sizeof(minuteText), "%02u", dt.minute);

  const bool largeScreen = w >= 460 && h >= 300;
  const int cardW = largeScreen ? std::min(w - 36, 560) : w;
  const int cardH = largeScreen ? 188 : std::max(1, h > 220 ? 170 : h);
  const int cardX = largeScreen ? x + (w - cardW) / 2 : x;
  const int cardY = y + (h - cardH) / 2;
  const int dateW =
      largeScreen ? clampInt(cardW * 25 / 100, 104, cardW - 220) : clampInt(cardW * 28 / 100, 44, cardW - 72);

  renderer.rectangle.fill(cardX, cardY, dateW, cardH, true);

  const int dateLineHeight = renderer.text.getLineHeight(LARGE_SYSTEM_FONT);
  const int dayY = cardY + (cardH - (dateLineHeight * 2 + 8)) / 2;
  const int monthY = dayY + dateLineHeight + 8;
  centerText(renderer, LARGE_SYSTEM_FONT, cardX, dateW, dayY, dayText, false, EpdFontFamily::BOLD);
  centerText(renderer, LARGE_SYSTEM_FONT, cardX, dateW, monthY, monthText, false);

  const int timeAreaX = cardX + dateW;
  const int timeAreaW = cardW - dateW;
  const int hourW = timeTextW(renderer, hourText, EpdFontFamily::BOLD, compact);
  const int colonW = timeTextW(renderer, ":", EpdFontFamily::REGULAR, compact);
  const int minuteW = timeTextW(renderer, minuteText, EpdFontFamily::REGULAR, compact);
  const int timeW = hourW + colonW + minuteW;
  const int timeX = timeAreaX + std::max(6, (timeAreaW - timeW) / 2);
  const int timeTop = cardY + (cardH - clockVisibleHeight(compact)) / 2;

  if (timeAreaW > 0 && timeW <= timeAreaW - 12) {
    renderClockSegment(renderer, timeX, timeTop, hourText, true, EpdFontFamily::BOLD, compact);
    renderClockSegment(renderer, timeX + hourW, timeTop, ":", true, EpdFontFamily::REGULAR, compact);
    renderClockSegment(renderer, timeX + hourW + colonW, timeTop, minuteText, true, EpdFontFamily::REGULAR, compact);
  } else if (timeAreaW > 48) {
    const int halfW = std::max(1, timeAreaW / 2);
    const int hourX = timeAreaX + std::max(2, (halfW - hourW) / 2);
    const int minuteX = timeAreaX + halfW + std::max(2, (timeAreaW - halfW - minuteW) / 2);
    renderClockSegment(renderer, hourX, timeTop, hourText, true, EpdFontFamily::BOLD, compact);
    renderClockSegment(renderer, minuteX, timeTop, minuteText, true, EpdFontFamily::REGULAR, compact);
    renderer.text.render(systemFontId(), timeAreaX + halfW - 2, std::max(0, timeTop + 42), ":", true);
  }
  if (SETTINGS.sleepClockTimeFormat == SystemSetting::CLOCK_12_HOUR) {
    renderer.text.render(TINY_FONT, timeAreaX + 18, std::max(0, cardY + cardH - 32), meridiem(dt), true,
                         EpdFontFamily::BOLD);
  }
}

}

namespace ClockRender {

const char* styleName(uint8_t style) {
  switch (style) {
    case SystemSetting::CLOCK_STACKED_CITY:
      return "Stacked";
    case SystemSetting::CLOCK_HORIZONTAL_CARD:
      return "Card";
    case SystemSetting::CLOCK_CENTERED_DATE:
    default:
      return "Centered";
  }
}

uint8_t styleCount() { return SystemSetting::SLEEP_CLOCK_STYLE_COUNT; }

void render(GfxRenderer& renderer, uint8_t style, const DateTimeView& dateTime, bool available, int x, int y, int w,
            int h, const bool compact) {
  if (!available) {
    renderUnavailable(renderer, x, y, w, h);
    return;
  }

  switch (style) {
    case SystemSetting::CLOCK_STACKED_CITY:
      renderStackedCity(renderer, dateTime, x, y, w, h, compact);
      break;
    case SystemSetting::CLOCK_HORIZONTAL_CARD:
      renderHorizontalCard(renderer, dateTime, x, y, w, h, compact);
      break;
    case SystemSetting::CLOCK_CENTERED_DATE:
    default:
      renderCenteredDate(renderer, dateTime, x, y, w, h, compact);
      break;
  }
}

}

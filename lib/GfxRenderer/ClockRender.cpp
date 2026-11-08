#include "ClockRender.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "../../src/state/SystemSetting.h"
#include "../../src/system/Fonts.h"

namespace {

constexpr const char* WEEKDAYS_SHORT[] = {"", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
constexpr const char* MONTHS_SHORT[] = {"",    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

int clockSystemFontId() {
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

int timeFont() { return MONTSERRAT_CLOCK_70_FONT_ID; }
int largeSystemFont() { return MONTSERRAT_18_FONT_ID; }
int tinyFont() { return MONTSERRAT_8_FONT_ID; }
constexpr int CLOCK_VISIBLE_HEIGHT = 106;
constexpr int CLOCK_GLYPH_TOP = 104;
constexpr uint8_t COMPACT_TIME_SCALE = 80;

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
  return compact ? renderer.text.getScaledWidth(timeFont(), text, COMPACT_TIME_SCALE, style)
                 : textW(renderer, timeFont(), text, style);
}

int clockRenderY(GfxRenderer& renderer, const int visibleTop, const bool compact) {
  if (!compact) return std::max(0, visibleTop - (renderer.text.getFontAscenderSize(timeFont()) - CLOCK_GLYPH_TOP));
  const int ascender = renderer.text.getFontAscenderSize(timeFont());
  const int glyphTop = ascender - renderer.text.getGlyphTopInset(timeFont(), '8', EpdFontFamily::BOLD);
  const int scaledGlyphTop = (glyphTop * COMPACT_TIME_SCALE + 50) / 100;
  return std::max(0, visibleTop - ascender + scaledGlyphTop);
}

void centerClockLine(GfxRenderer& renderer, const int x, const int w, const int visibleTop, const char* text,
                     const bool black, const bool compact) {
  if (compact) {
    const int scaledWidth = timeTextW(renderer, text, EpdFontFamily::BOLD, true);
    renderer.text.renderScaled(timeFont(), x + (w - scaledWidth) / 2, clockRenderY(renderer, visibleTop, true), text,
                               COMPACT_TIME_SCALE, black, EpdFontFamily::BOLD);
    return;
  }
  centerText(renderer, timeFont(), x, w, clockRenderY(renderer, visibleTop, false), text, black,
             EpdFontFamily::BOLD);
}

void renderClockLine(GfxRenderer& renderer, const int x, const int visibleTop, const char* text, const bool compact) {
  if (compact) {
    renderer.text.renderScaled(timeFont(), std::max(0, x), clockRenderY(renderer, visibleTop, true), text,
                               COMPACT_TIME_SCALE, true, EpdFontFamily::BOLD);
    return;
  }
  renderer.text.render(timeFont(), std::max(0, x), clockRenderY(renderer, visibleTop, false), text, true,
                       EpdFontFamily::BOLD);
}

void renderClockSegment(GfxRenderer& renderer, const int x, const int visibleTop, const char* text, const bool black,
                        const EpdFontFamily::Style style, const bool compact) {
  if (compact) {
    renderer.text.renderScaled(timeFont(), std::max(0, x), clockRenderY(renderer, visibleTop, true), text,
                               COMPACT_TIME_SCALE, black, style);
    return;
  }
  renderer.text.render(timeFont(), std::max(0, x), clockRenderY(renderer, visibleTop, false), text, black, style);
}

void renderUnavailable(GfxRenderer& renderer, int x, int y, int w, int h) {
  const int centerY = y + h / 2;
  centerText(renderer, clockSystemFontId(), x, w, centerY - 28, "Clock unavailable", true, EpdFontFamily::BOLD);
  centerText(renderer, tinyFont(), x, w, centerY + 8, "Sync time in settings");
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
  renderer.text.renderGray(largeSystemFont(), infoX, std::max(0, top + 18), weekdayShort(dt.weekday), true,
                           EpdFontFamily::BOLD);
  renderer.text.render(largeSystemFont(), infoX, std::max(0, top + 58), dateText, true);
  if (SETTINGS.sleepClockTimeFormat == SystemSetting::CLOCK_12_HOUR) {
    renderer.text.renderGray(largeSystemFont(), infoX, std::max(0, top + 120), meridiem(dt), true,
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
      renderer.text.renderScaledGray(timeFont(), std::max(0, timeX + hourW + colonW),
                                     clockRenderY(renderer, timeTop, true), minuteText, COMPACT_TIME_SCALE, true,
                                     EpdFontFamily::REGULAR);
    } else {
      renderer.text.renderGray(timeFont(), std::max(0, timeX + hourW + colonW), clockRenderY(renderer, timeTop, false),
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
      renderer.text.renderScaledGray(timeFont(), std::max(0, minuteX), std::max(0, minuteY), minuteText,
                                     COMPACT_TIME_SCALE, true, EpdFontFamily::BOLD);
    } else {
      renderer.text.renderGray(timeFont(), std::max(0, minuteX), std::max(0, minuteY), minuteText, true,
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

  constexpr int compactCardLeftPadding = 20;
  const int contentX = x + (compact ? compactCardLeftPadding : 0);
  const int contentW = std::max(1, w - (compact ? compactCardLeftPadding : 0));
  const bool largeScreen = contentW >= 460 && h >= 300;
  const int cardW = largeScreen ? std::min(contentW - 36, 560) : contentW;
  const int cardH = largeScreen ? 188 : std::max(1, h > 220 ? 170 : h);
  const int cardX = largeScreen ? contentX + (contentW - cardW) / 2 : contentX;
  const int cardY = y + (h - cardH) / 2;
  const int dateW =
      largeScreen ? clampInt(cardW * 25 / 100, 104, cardW - 220) : clampInt(cardW * 28 / 100, 44, cardW - 72);

  renderer.rectangle.fill(cardX, cardY, dateW, cardH, static_cast<int>(GfxRenderer::FillTone::Gray));

  const int dateLineHeight = renderer.text.getLineHeight(largeSystemFont());
  const int dayY = cardY + (cardH - (dateLineHeight * 2 + 8)) / 2;
  const int monthY = dayY + dateLineHeight + 8;
  centerText(renderer, largeSystemFont(), cardX, dateW, dayY, dayText, false, EpdFontFamily::BOLD);
  centerText(renderer, largeSystemFont(), cardX, dateW, monthY, monthText, false);

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
    if (compact) {
      renderer.text.renderScaledGray(timeFont(), timeX + hourW + colonW, clockRenderY(renderer, timeTop, true),
                                     minuteText, COMPACT_TIME_SCALE, true, EpdFontFamily::REGULAR);
    } else {
      renderer.text.renderGray(timeFont(), timeX + hourW + colonW, clockRenderY(renderer, timeTop, false), minuteText,
                               true, EpdFontFamily::REGULAR);
    }
  } else if (timeAreaW > 48) {
    const int halfW = std::max(1, timeAreaW / 2);
    const int hourX = timeAreaX + std::max(2, (halfW - hourW) / 2);
    const int minuteX = timeAreaX + halfW + std::max(2, (timeAreaW - halfW - minuteW) / 2);
    renderClockSegment(renderer, hourX, timeTop, hourText, true, EpdFontFamily::BOLD, compact);
    if (compact) {
      renderer.text.renderScaledGray(timeFont(), minuteX, clockRenderY(renderer, timeTop, true), minuteText,
                                     COMPACT_TIME_SCALE, true, EpdFontFamily::REGULAR);
    } else {
      renderer.text.renderGray(timeFont(), minuteX, clockRenderY(renderer, timeTop, false), minuteText, true,
                               EpdFontFamily::REGULAR);
    }
    renderer.text.render(clockSystemFontId(), timeAreaX + halfW - 2, std::max(0, timeTop + 42), ":", true);
  }
  if (SETTINGS.sleepClockTimeFormat == SystemSetting::CLOCK_12_HOUR) {
    renderer.text.render(tinyFont(), timeAreaX + 18, std::max(0, cardY + cardH - 32), meridiem(dt), true,
                         EpdFontFamily::BOLD);
  }
}

void renderWeekdaysTime(GfxRenderer& renderer, const ClockRender::DateTimeView& dt, int x, int y, int w, int h,
                        const bool compact) {
  char timeText[8];
  formatTime(dt, timeText, sizeof(timeText));

  constexpr int weekdayGap = 12;
  const int weekdayFont = MONTSERRAT_10_FONT_ID;
  constexpr int weekdayCount = 7;
  const int weekdayLine = renderer.text.getLineHeight(weekdayFont);
  const int visibleTimeHeight = clockVisibleHeight(compact);
  const int contentHeight = weekdayLine + weekdayGap + visibleTimeHeight;
  const int contentTop = y + std::max(0, (h - contentHeight) / 2);

  // Keep the complete Monday through Sunday row with compact spacing.
  constexpr int firstWeekday = 1;
  constexpr int weekdayLabelGap = 5;
  uint8_t weekdays[weekdayCount];
  int labelWidths[weekdayCount];
  int labelsWidth = 0;
  for (int index = 0; index < weekdayCount; ++index) {
    weekdays[index] = static_cast<uint8_t>(((firstWeekday - 1 + index) % 7) + 1);
    const bool current = dt.weekday == weekdays[index];
    const EpdFontFamily::Style labelStyle = current ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    labelWidths[index] = textW(renderer, weekdayFont, weekdayShort(weekdays[index]), labelStyle);
    labelsWidth += labelWidths[index];
  }
  labelsWidth += weekdayLabelGap * (weekdayCount - 1);
  int labelX = x + std::max(0, (w - labelsWidth) / 2);
  for (int index = 0; index < weekdayCount; ++index) {
    const uint8_t weekday = weekdays[index];
    const bool current = dt.weekday == weekday;
    const char* label = weekdayShort(weekday);
    const EpdFontFamily::Style labelStyle = current ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    if (current) {
      renderer.text.render(weekdayFont, labelX, contentTop, label, true, labelStyle);
    } else {
      renderer.text.renderGray(weekdayFont, labelX, contentTop, label, true, labelStyle);
    }
    labelX += labelWidths[index] + weekdayLabelGap;
  }

  centerClockLine(renderer, x, w, contentTop + weekdayLine + weekdayGap, timeText, true, compact);
}

}

namespace ClockRender {

const char* styleName(uint8_t style) {
  switch (style) {
    case SystemSetting::CLOCK_STACKED_CITY:
      return "Stacked";
    case SystemSetting::CLOCK_HORIZONTAL_CARD:
      return "Card";
    case SystemSetting::CLOCK_WEEKDAYS_TIME:
      return "Days";
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
    case SystemSetting::CLOCK_WEEKDAYS_TIME:
      renderWeekdaysTime(renderer, dateTime, x, y, w, h, compact);
      break;
    case SystemSetting::CLOCK_CENTERED_DATE:
    default:
      renderCenteredDate(renderer, dateTime, x, y, w, h, compact);
      break;
  }
}

}

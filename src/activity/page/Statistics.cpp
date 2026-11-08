/**
 * @file Statistics.cpp
 * @brief Definitions for Statistics.
 */

#include "Statistics.h"
#include "system/UiLayout.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <ImageRender.h>
#include <SDCardManager.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <esp_task_wdt.h>

#include "state/BookState.h"
#include "state/RecentBooks.h"
#include "state/ReadingGoal.h"
#include "state/SystemSetting.h"
#include "activity/page/components/global/Button.h"
#include "components/widget/BaseCarousel.h"
#include "components/global/Sidebar.h"
#include "images/Close.h"
#include "images/Hamburger.h"
#include "images/Calendar.h"
#include "images/DeviceDisplay.h"
#include "images/LibraryFilterRight.h"
#include "images/Refresh.h"
#include "images/StatsPie.h"
#include "images/StatisticsPeriodToggle.h"
#include <Epub/BookMetadataCache.h>
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"
#include "util/LibraryIndexRefresh.h"

extern void onGoToHome();

Statistics::Statistics(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> close)
    : SubPage("Statistics", renderer, mappedInput, std::move(close)), viewIndex(0) {}

namespace {

int fontSansSm() { return systemFontIdForPointSize(8); }
int fontSerif() { return systemFontIdForPointSize(14); }
int fontSerifMd() { return systemFontIdForPointSize(16); }
int fontSerifLg() { return systemFontIdForPointSize(18); }
int fontSerifSm() { return systemFontIdForPointSize(12); }
constexpr float kPi = 3.14159265f;

constexpr int kCalendarGap = 5;

constexpr int kStatsHeaderBottom = (FREEINK_DEVICE_X4PRO ? 20 : 15) + 40 + 20;
constexpr int kStatsContentTop = kStatsHeaderBottom + 10;
constexpr int kCalendarContentTop = kStatsHeaderBottom;
constexpr int kWeeklyChartTop = kStatsHeaderBottom + 2;
constexpr int kStatsContentBottomPadding = 12;
constexpr int kBooksPerPage = 5;
constexpr int kBooksRowHeight = 132;
constexpr int kMonthlyMaxBarWidth = 28;

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

void drawStatisticsLine(GfxRenderer& renderer, int x1, int y1, const int x2, const int y2) {
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

std::string formatBookHistoryDate(const uint32_t dateKey) {
  constexpr const char* kMonths[] = {"",    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  const unsigned month = static_cast<unsigned>((dateKey / 100U) % 100U);
  const unsigned day = static_cast<unsigned>(dateKey % 100U);
  char text[16];
  if (month >= 1 && month <= 12 && day >= 1 && day <= 31) {
    std::snprintf(text, sizeof(text), "%s %u", kMonths[month], day);
  } else {
    std::snprintf(text, sizeof(text), "--");
  }
  return text;
}

int daysInMonth(const int year, const unsigned month) {
  constexpr int kDaysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month != 2) return kDaysPerMonth[month - 1];
  const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  return leap ? 29 : 28;
}

int64_t currentStatisticsDay() {
  uint32_t dateKey = 0;
  if (currentRtcDateKey(dateKey)) {
    return readingDateToDay(dateKey);
  }

  int64_t latestDay = 0;
  for (const ReadingHistoryEntry& entry : getReadingHistory()) {
    latestDay = std::max(latestDay, readingDateToDay(entry.dateKey));
  }
  return latestDay;
}

int64_t monthStartDay(const int64_t day) {
  if (day <= 0) return 0;
  const uint32_t dateKey = readingDayToDate(day);
  const int year = static_cast<int>(dateKey / 10000UL);
  const unsigned month = static_cast<unsigned>((dateKey / 100UL) % 100UL);
  return readingDateToDay(static_cast<uint32_t>(year) * 10000UL + month * 100UL + 1UL);
}

ButtonBounds nextButton(const GfxRenderer& renderer) {
  return {renderer.getScreenWidth() - 80, renderer.getScreenHeight() - 20 - Button::height, 60, Button::height};
}

ButtonBounds statisticsRefreshButton(const GfxRenderer& renderer) {
  constexpr int kButtonSize = 66;
  constexpr int kRightMargin = 0;
  constexpr int kBottomMargin = 0;
  return {renderer.getScreenWidth() - kRightMargin - kButtonSize,
          renderer.getScreenHeight() - kBottomMargin - kButtonSize,
          kButtonSize, kButtonSize};
}

ButtonBounds statisticsChartPeriodButton(const GfxRenderer& renderer, const int chartTop) {
  constexpr int kButtonSize = 50;
  return {renderer.getScreenWidth() / 2 - kButtonSize / 2, chartTop + 2, kButtonSize, kButtonSize};
}

struct SingleBookLayout {
  int areaY;
  int topH;
  int middleH;
  int bottomH;
};

SingleBookLayout singleBookLayout(const GfxRenderer& renderer) {
  const int areaY = kStatsContentTop;
  const int areaH = std::max(0, renderer.getScreenHeight() - areaY - kStatsContentBottomPadding);
  const int baseH = areaH / 3;
  const int topH = std::max(1, (baseH * 90) / 100);
  const int transferredH = baseH - topH;
  const int middleH = baseH + transferredH;
  const int bottomH = std::max(0, areaH - topH - middleH);
  return {areaY, topH, middleH, bottomH};
}

void drawStatisticsRefreshButton(const GfxRenderer& renderer) {
  const ButtonBounds refresh = statisticsRefreshButton(renderer);
  renderer.rectangle.fill(refresh.x, refresh.y, refresh.width, refresh.height,
                          static_cast<int>(GfxRenderer::FillTone::Ink), true, true);
  renderer.bitmap.icon(Refresh, refresh.x + (refresh.width - 40) / 2, refresh.y + (refresh.height - 40) / 2,
                       40, 40, BitmapRender::Orientation::None, true);
}

void drawStatisticsChartPeriodButton(const GfxRenderer& renderer, const int chartTop) {
  const ButtonBounds bounds = statisticsChartPeriodButton(renderer, chartTop);
  renderer.rectangle.fill(bounds.x, bounds.y, bounds.width, bounds.height,
                          static_cast<int>(GfxRenderer::FillTone::Paper), true);
  renderer.rectangle.render(bounds.x, bounds.y, bounds.width, bounds.height, true, true);
  constexpr int iconSize = 40;
  renderer.bitmap.icon(StatisticsPeriodToggle, bounds.x + (bounds.width - iconSize) / 2,
                       bounds.y + (bounds.height - iconSize) / 2, iconSize, iconSize);
}

struct StatisticsAction {
  const uint8_t* icon;
  const char* label;
};

constexpr StatisticsAction kStatisticsActions[] = {
    {Calendar, "Calendar"}, {StatsPie, "Stats"}, {DeviceDisplay, "Books"}};

static std::string epubCachePathForBookPath(const std::string& bookPath) {
  return "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(bookPath));
}

std::string plainBookDescription(const std::string& raw) {
  std::string plain;
  plain.reserve(raw.size());
  bool inTag = false;
  bool pendingSpace = false;
  for (const char character : raw) {
    if (character == '<') {
      inTag = true;
      pendingSpace = true;
      continue;
    }
    if (character == '>') {
      inTag = false;
      continue;
    }
    if (inTag) {
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(character))) {
      pendingSpace = true;
      continue;
    }
    if (pendingSpace && !plain.empty()) {
      plain.push_back(' ');
    }
    pendingSpace = false;
    plain.push_back(character);
  }
  return plain;
}

std::string thumbnailPathForBookCache(const std::string& cachePath) {
  constexpr const char* thumbnailNames[] = {"thumb.jpg", "thumb.png", "thumb.bmp"};
  for (const char* thumbnailName : thumbnailNames) {
    const std::string path = cachePath + "/" + thumbnailName;
    if (SdMan.exists(path.c_str())) {
      return path;
    }
  }
  return {};
}

std::vector<BookListMetadata> buildBookListMetadata(const std::vector<BookReadingStats>& books) {
  std::vector<BookListMetadata> metadataList;
  metadataList.reserve(books.size());
  for (size_t index = 0; index < books.size(); ++index) {
    if ((index & 7u) == 0u) {
      esp_task_wdt_reset();
      yield();
    }
    const BookReadingStats& book = books[index];
    BookListMetadata metadata;
    metadata.thumbnailPath = thumbnailPathForBookCache(book.path);
    metadata.title = book.title;
    metadata.progressPercent = book.progressPercent;

    BookMetadataCache bookMetadata(book.path);
    if (bookMetadata.load()) {
      if (metadata.title.empty()) {
        metadata.title = bookMetadata.coreMetadata.title;
      }
      metadata.description = plainBookDescription(bookMetadata.coreMetadata.description);
    }

    if (metadata.title.empty()) {
      metadata.title = "Untitled";
    }
    if (metadata.description.empty()) {
      metadata.description = "No description available";
    }
    metadataList.push_back(std::move(metadata));
  }
  return metadataList;
}

void mergeFinishedBookStatuses(GlobalReadingStats& global, const std::vector<BookReadingStats>& allBooks) {
  const std::vector<BookState::Book> finishedBooks = BOOK_STATE.getFinishedBooks();
  if (finishedBooks.empty()) {
    return;
  }

  RECENT_BOOKS.loadFromFile();

  std::unordered_set<std::string> statCachePaths;
  std::unordered_set<std::string> progressFinishedCachePaths;
  for (const BookReadingStats& book : allBooks) {
    statCachePaths.insert(book.path);
    if (book.progressPercent >= 99.0f) {
      progressFinishedCachePaths.insert(book.path);
    }
  }

  std::unordered_set<std::string> finishedCachePaths;
  for (const BookState::Book& book : finishedBooks) {
    if (book.path.empty()) {
      continue;
    }
    finishedCachePaths.insert(epubCachePathForBookPath(book.path));
    for (const RecentBook& recent : RECENT_BOOKS.getBooks()) {
      if (recent.path != book.path) {
        continue;
      }
      if (!recent.cachePath.empty()) {
        finishedCachePaths.insert(recent.cachePath);
      }
      break;
    }
  }

  for (const std::string& cachePath : finishedCachePaths) {
    if (statCachePaths.count(cachePath) != 0) {
      if (progressFinishedCachePaths.count(cachePath) == 0) {
        global.totalBooksFinished++;
      }
      continue;
    }

    // A finished flag can outlive a missing statistics record. Keep the
    // global started/finished totals internally consistent in that case.
    global.totalBooksStarted++;
    global.totalBooksFinished++;
  }
}

/** Global “All items” donut row (must match measureAllItemsBodyHeight). */
constexpr int kGlobalAllItemsDonutR = 78;
constexpr int kGlobalAllItemsDonutThick = 10;
constexpr int kGlobalAllItemsDonutPadT = 14;
constexpr int kGlobalAllItemsDonutPadB = 14;
constexpr int kGlobalAllItemsPadSide = 12;
constexpr int kGlobalAllItemsDonutTextGap = 10;

void drawThinProgressBar(const GfxRenderer& renderer, int x, int y, int w, int h, float pct01) {
  if (w <= 0 || h <= 0) {
    return;
  }
  renderer.rectangle.fill(x, y, w, h, static_cast<int>(GfxRenderer::FillTone::Gray), false);
  const float p = std::min(1.f, std::max(0.f, pct01));
  const int fillW = static_cast<int>(static_cast<float>(w) * p + 0.5f);
  if (fillW > 0) {
    renderer.rectangle.fill(x, y, fillW, h, static_cast<int>(GfxRenderer::FillTone::Ink), false);
  }
  renderer.rectangle.dotted(x, y, w, h, true);
}

/** Circle outline via pixels (GfxRenderer::drawLine does not draw diagonals). */
void drawCircleOutlinePixels(const GfxRenderer& renderer, int cx, int cy, int radius) {
  if (radius < 1) {
    return;
  }
  const int n = std::max(120, radius * 6);
  for (int i = 0; i < n; ++i) {
    const float ang = static_cast<float>(i) / static_cast<float>(n) * (2.f * kPi);
    const int px = cx + static_cast<int>(radius * cosf(ang) + 0.5f);
    const int py = cy + static_cast<int>(radius * sinf(ang) + 0.5f);
    renderer.drawPixel(px, py, true);
  }
}

static float normAngle0TwoPi(float a) {
  const float twoPi = 2.f * kPi;
  while (a < 0.f) {
    a += twoPi;
  }
  while (a >= twoPi) {
    a -= twoPi;
  }
  return a;
}

/** CCW angle from a0 to ang, in [0, 2pi). */
static float ccwDeltaFrom(float ang, float a0) {
  const float twoPi = 2.f * kPi;
  const float A = normAngle0TwoPi(ang);
  const float F = normAngle0TwoPi(a0);
  float d = A - F;
  if (d < 0.f) {
    d += twoPi;
  }
  return d;
}

static bool inSweepCcw(float ang, float a0, float sweepRad) {
  const float twoPi = 2.f * kPi;
  if (sweepRad >= twoPi - 0.002f) {
    return true;
  }
  /** Slightly generous so discrete pixels on the wedge edge are not left as background gaps. */
  return ccwDeltaFrom(ang, a0) <= sweepRad + 0.008f;
}

/** Solid ink in annulus wedge (GfxRenderer has no diagonal drawLine for radials/arcs). */
void fillAnnulusWedgeInkPixels(const GfxRenderer& renderer, int cx, int cy, int ro, int ri, float a0, float sweepRad) {
  if (ri >= ro || sweepRad <= 0.f) {
    return;
  }
  const float twoPi = 2.f * kPi;
  const bool full = sweepRad >= twoPi - 0.002f;
  const long rlo = static_cast<long>(ri) * ri;
  const long rhi = static_cast<long>(ro) * ro;
  const int x0 = cx - ro;
  const int x1 = cx + ro;
  const int y0 = cy - ro;
  const int y1 = cy + ro;
  for (int py = y0; py <= y1; ++py) {
    const long dy = static_cast<long>(py - cy);
    const long dy2 = dy * dy;
    for (int px = x0; px <= x1; ++px) {
      const long dx = static_cast<long>(px - cx);
      const long r2 = dx * dx + dy2;
      /** Inclusive outer chord closes 1px gaps vs scanline gray fill; strict inner keeps the hole. */
      if (r2 <= rlo || r2 > rhi) {
        continue;
      }
      if (!full) {
        const float ang = atan2f(static_cast<float>(py - cy), static_cast<float>(px - cx));
        if (!inSweepCcw(ang, a0, sweepRad)) {
          continue;
        }
      }
      renderer.drawPixel(px, py, true);
    }
  }
}

/** Annulus filled row-by-row (reliable on e-ink; avoids polygon / radial artifacts). */
void fillAnnulusToneScanlines(const GfxRenderer& renderer, int cx, int cy, int ro, int ri, GfxRenderer::FillTone tone) {
  if (ri >= ro || ro < 2) {
    return;
  }
  const int scrW = renderer.getScreenWidth();
  const int scrH = renderer.getScreenHeight();
  const int y0 = std::max(0, cy - ro);
  const int y1 = std::min(scrH - 1, cy + ro);
  for (int y = y0; y <= y1; ++y) {
    const int dy = y - cy;
    const int ady = std::abs(dy);
    if (ady >= ro) {
      continue;
    }
    const long ro2 = static_cast<long>(ro) * ro;
    const long dy2 = static_cast<long>(dy) * dy;
    const long xo2 = ro2 - dy2;
    if (xo2 < 0) {
      continue;
    }
    /** Ceil outer half-chord, floor inner half-chord so gray ring meets ink wedge without 1px white seams. */
    const int xo = static_cast<int>(ceilf(sqrtf(static_cast<float>(xo2)) - 1e-3f));
    auto span = [&](int xa, int xb) {
      if (xa > xb) {
        std::swap(xa, xb);
      }
      xa = std::max(0, xa);
      xb = std::min(scrW - 1, xb);
      if (xa <= xb) {
        renderer.rectangle.fill(xa, y, xb - xa + 1, 1, static_cast<int>(tone), false);
      }
    };
    if (ady >= ri) {
      span(cx - xo, cx + xo);
    } else {
      const long ri2 = static_cast<long>(ri) * ri;
      const long xi2 = ri2 - dy2;
      int xi = static_cast<int>(floorf(sqrtf(static_cast<float>(xi2)) + 1e-3f));
      xi = std::min(xi, xo);
      span(cx - xo, cx - xi);
      span(cx + xi, cx + xo);
    }
  }
}

void drawFullDonutGauge(const GfxRenderer& renderer, int cx, int cy, int rOut, int thick, float pct01,
                        const char* centerPct) {
  const int thickMin = 6;
  const int thickMax = std::max(thickMin + 2, rOut - 8);
  const int thickUse = std::max(thickMin, std::min(thickMax, thick));
  const int rIn = rOut - thickUse;
  if (rIn <= 2 || rOut <= rIn + 2) {
    return;
  }
  const float twoPi = 2.f * kPi;
  /** Fill slightly inside outer/inner ink outlines so the ring reads solid (drawLine has no diagonals). */
  const int roFill = rOut - 1;
  const int riFill = rIn + 1;
  if (roFill > riFill + 1) {
    fillAnnulusToneScanlines(renderer, cx, cy, roFill, riFill, GfxRenderer::FillTone::Gray);
  }

  const float p = std::min(1.f, std::max(0.f, pct01));
  const float a0 = -kPi / 2.f;
  if (p >= 0.999f) {
    fillAnnulusToneScanlines(renderer, cx, cy, roFill, riFill, GfxRenderer::FillTone::Ink);
  } else if (p > 0.004f) {
    /** Tiny angular overlap so the ink wedge meets the gray ring at the start/end radii on a pixel grid. */
    constexpr float kSweepSlopRad = 0.02f;
    const float sweep = std::min(twoPi, p * twoPi + kSweepSlopRad);
    fillAnnulusWedgeInkPixels(renderer, cx, cy, roFill, riFill, a0 - kSweepSlopRad * 0.5f, sweep);
  }

  /** Outlines at real outer/inner radii only (no extra circles inside the hole). */
  drawCircleOutlinePixels(renderer, cx, cy, rOut);
  drawCircleOutlinePixels(renderer, cx, cy, rIn);

  const int tw = renderer.text.getWidth(fontSerifMd(), centerPct);
  const int lhMd = renderer.text.getLineHeight(fontSerifMd());
  renderer.text.render(fontSerifMd(), cx - tw / 2, cy - lhMd / 2, centerPct);
}

/** Shared geometry for the global “all items” donut + metrics block (must stay in sync across draw paths). */
struct GlobalAllItemsGeom {
  int lhSans;
  int lhNum;
  int kCaptionStackH;
  int kMetricsPadT;
  int kMetricsH;
  int rowH;
};

static GlobalAllItemsGeom computeGlobalAllItemsGeom(const GfxRenderer& renderer) {
  const int fontSans = systemFontId();
  const int lhSans = renderer.text.getLineHeight(fontSans);
  const int lhSm = renderer.text.getLineHeight(fontSansSm());
  const int lhNum = renderer.text.getLineHeight(fontSerifLg());
  const int kCaptionStackH = lhSans * 2 + 6;
  constexpr int kMetricsPadT = 8;
  const int kMetricsH = kMetricsPadT + lhNum + 4 + lhSm;
  const int rowH =
      std::max(kGlobalAllItemsDonutPadT + kGlobalAllItemsDonutR + kGlobalAllItemsDonutR + kGlobalAllItemsDonutPadB,
               kCaptionStackH + 4);
  return {lhSans, lhNum, kCaptionStackH, kMetricsPadT, kMetricsH, rowH};
}

/** Donut row with its caption centered together in the inner band. */
static void drawGlobalAllItemsGaugeRow(const GfxRenderer& renderer, int innerLeft, int innerRight, int y,
                                       float finishedRatio01, const GlobalAllItemsGeom& g,
                                       const bool alignLeft = false) {
  const int innerW = innerRight - innerLeft;
  const char* line1 = "of your books";
  const char* line2 = "are finished.";
  const int font = systemFontId();
  const int textW = std::max(renderer.text.getWidth(font, line1), renderer.text.getWidth(font, line2));
  const int groupW = kGlobalAllItemsDonutR * 2 + kGlobalAllItemsDonutTextGap + textW;
  const int groupX = alignLeft ? innerLeft : innerLeft + std::max(0, (innerW - groupW) / 2);
  const int cx = groupX + kGlobalAllItemsDonutR;
  const int cy = y + kGlobalAllItemsDonutPadT + kGlobalAllItemsDonutR;
  const int xText = cx + kGlobalAllItemsDonutR + kGlobalAllItemsDonutTextGap;
  const int yText0 = y + (g.rowH - g.kCaptionStackH) / 2;

  char pct[16];
  snprintf(pct, sizeof(pct), "%.0f%%", finishedRatio01 * 100.f);
  drawFullDonutGauge(renderer, cx, cy, kGlobalAllItemsDonutR, kGlobalAllItemsDonutThick, finishedRatio01, pct);
  renderer.text.render(font, xText, yText0, line1);
  renderer.text.render(font, xText, yText0 + g.lhSans, line2);
}

/**
 * Horizontal rule + two-column metrics (second band).
 * yRulePreferred: caller’s ideal rule Y (after gauge + gap).
 * yRuleMin: never place the rule above this (prevents the old min(y, yEnd-h) clamp from erasing the gap under the
 * gauge).
 */
static int drawGlobalAllItemsSecondBand(const GfxRenderer& renderer, int innerLeft, int innerRight, int yRulePreferred,
                                        int yRuleMin, int yContentEnd, uint32_t booksFinished, uint32_t booksOpened,
                                        const GlobalAllItemsGeom& g) {
  const int innerW = innerRight - innerLeft;
  const int yMaxRule = yContentEnd - g.kMetricsH - 2;
  /** Prefer the caller’s Y, never above yMaxRule, never below yRuleMin when there is room (old code only did min→yMax,
   * which stole the gap under the gauge). */
  const int capPref = std::min(yRulePreferred, yMaxRule);
  int yRule = std::min(yMaxRule, std::max(yRuleMin, capPref));
  const int midX = innerLeft + innerW / 2;

  char buf[32];
  snprintf(buf, sizeof(buf), "%u", booksFinished);
  const int leftCx = innerLeft + (midX - innerLeft) / 2;
  const int twFin = renderer.text.getWidth(fontSerifLg(), buf);
  const int yNum = yRule + g.kMetricsPadT;
  renderer.text.render(fontSerifLg(), leftCx - twFin / 2, yNum, buf);
  const char* labFin = "Books finished";
  const int twLabF = renderer.text.getWidth(fontSansSm(), labFin);
  const int yLabFin = yNum + g.lhNum + 4;
  renderer.text.render(fontSansSm(), leftCx - twLabF / 2, yLabFin, labFin);

  snprintf(buf, sizeof(buf), "%u", booksOpened);
  const int rightCx = midX + (innerRight - midX) / 2;
  const int twH = renderer.text.getWidth(fontSerifLg(), buf);
  renderer.text.render(fontSerifLg(), rightCx - twH / 2, yNum, buf);
  const char* labH = "Books opened";
  const int twLabH = renderer.text.getWidth(fontSansSm(), labH);
  renderer.text.render(fontSansSm(), rightCx - twLabH / 2, yLabFin, labH);

  return yRule + g.kMetricsH;
}

/**
 * Two-column grid with `numRows` rows: value (serif lg) + label (sans sm) per cell.
 * Each column width stops short of the center rule so labels never bleed across.
 * Row 0 is shifted up by `row0LiftPx` only; other rows unchanged. Horizontal rules between rows.
 * @param row0LiftPx use 0 when this grid must not intrude into the margin above `y` (e.g. global stats layout).
 * @param gapBeforeLastRowPx when numRows > 2, extra space inserted after the second-to-last row so the horizontal
 *        rule above the bottom row sits lower (avoids overlap between the middle row labels and the pages row).
 */
int drawFourColumnStatsNx2(const GfxRenderer& renderer, int innerLeft, int y, int innerW, const char* const* vals,
                           const char* const* labs, int numRows, int cellH, int row0LiftPx,
                           int gapBeforeLastRowPx = 0) {
  if (numRows < 1) {
    return 0;
  }
  constexpr int kPadBelowMidRule = 2;
  const int halfW = innerW / 2;
  const int midX = innerLeft + halfW;
  const int gap = (numRows > 2 ? gapBeforeLastRowPx : 0);
  std::vector<int> yBound(static_cast<size_t>(numRows + 1), y);
  for (int r = 1; r <= numRows; ++r) {
    yBound[static_cast<size_t>(r)] = yBound[static_cast<size_t>(r - 1)] + cellH + ((r == numRows - 1) ? gap : 0);
  }
  const int blockH = yBound[static_cast<size_t>(numRows)] - y;
  constexpr int kEdgePad = 8;
  constexpr int kMidGutter = 10;
  constexpr int kCellVMarginBottom = 4;
  constexpr int kCellPadTop = 0;
  const int wLeft = std::max(20, midX - innerLeft - kEdgePad - kMidGutter);
  const int wRight = std::max(20, (innerLeft + innerW) - midX - kEdgePad - kMidGutter);
  const int lhVal = renderer.text.getLineHeight(fontSerifLg());
  const int lhLab = renderer.text.getLineHeight(fontSansSm());
  constexpr int kValLabGap = 4;
  const int stackH = lhVal + kValLabGap + lhLab;

  auto cell = [&](int col, int row, const char* val, const char* lab) {
    const int cellLeft = (col == 0) ? (innerLeft + kEdgePad) : (midX + kMidGutter);
    const int cw = (col == 0) ? wLeft : wRight;
    const int bandTop = yBound[static_cast<size_t>(row)] + (row == 0 ? kCellPadTop : kPadBelowMidRule);
    const int bandBottom = yBound[static_cast<size_t>(row + 1)] - kCellVMarginBottom;
    const int innerBand = std::max(1, bandBottom - bandTop);
    int rowTop;
    if (row == 0) {
      const int floorY = std::max(0, y - row0LiftPx);
      rowTop = bandTop - row0LiftPx;
      if (rowTop < floorY) {
        rowTop = floorY;
      }
      if (rowTop + stackH > bandBottom) {
        rowTop = bandBottom - stackH;
      }
    } else {
      rowTop = (stackH <= innerBand) ? bandTop : std::max(bandTop, bandBottom - stackH);
    }
    const std::string valT = renderer.text.truncate(fontSerifLg(), val, cw);
    const std::string labT = renderer.text.truncate(fontSansSm(), lab, cw);
    renderer.text.render(fontSerifLg(), cellLeft, rowTop, valT.c_str());
    renderer.text.render(fontSansSm(), cellLeft, rowTop + lhVal + kValLabGap, labT.c_str());
  };
  for (int row = 0; row < numRows; ++row) {
    cell(0, row, vals[row * 2], labs[row * 2]);
    cell(1, row, vals[row * 2 + 1], labs[row * 2 + 1]);
  }
  return blockH;
}

int drawFourColumnStats2x2(const GfxRenderer& renderer, int innerLeft, int y, int innerW, const char* v0,
                           const char* l0, const char* v1, const char* l1, const char* v2, const char* l2,
                           const char* v3, const char* l3, int cellH, int row0LiftPx) {
  const char* vals[] = {v0, v1, v2, v3};
  const char* labs[] = {l0, l1, l2, l3};
  // Preserve the original left-aligned 2x2 layout while giving the two rows
  // the requested 20 px vertical separation.
  constexpr int kVerticalGap = 20;
  return drawFourColumnStatsNx2(renderer, innerLeft, y, innerW, vals, labs, 2, cellH, row0LiftPx, kVerticalGap);
}

}

void Statistics::loadStats(const bool showProgress) {
  ScreenComponents::LoadingProgressLayout layout{};
  if (showProgress) {
    layout = ScreenComponents::LoadingProgress::show(renderer, "Loading statistics...", 12);
  }
  const auto setProgress = [this, &layout, showProgress](const int progress) {
    if (showProgress) {
      ScreenComponents::LoadingProgress::setProgress(renderer, layout, progress);
    }
  };
  allBooksStats = getAllBooksStats();
  allBooksStats.erase(
      std::remove_if(allBooksStats.begin(), allBooksStats.end(), [](const BookReadingStats& book) {
        // A zero-progress cache is normally created when a book is opened
        // before the first page is read. It is not a useful statistics entry
        // and should not pollute the Books list or aggregate totals.
        return !(book.progressPercent > 0.0f);
      }),
      allBooksStats.end());
  loadedBookStatsFlags_.assign(allBooksStats.size(), 1);
  setProgress(40);
  std::sort(allBooksStats.begin(), allBooksStats.end(),
            [](const BookReadingStats& a, const BookReadingStats& b) { return a.lastReadTimeMs > b.lastReadTimeMs; });
  setProgress(65);
  globalStats = aggregateGlobalStatsFromBooks(allBooksStats);
  mergeFinishedBookStatuses(globalStats, allBooksStats);
  bookListMetadata_ = buildBookListMetadata(allBooksStats);
  saveBookListMetadata(bookListMetadata_);
  setProgress(90);
  viewIndex = 0;
  booksPage_ = 0;
  setProgress(100);
  saveGlobalStats(globalStats);
  refreshChartData();
}

void Statistics::refreshWeeklyChartData() {
  weeklyReadingTimeMs_.fill(0);
  weeklyTotalReadingTimeMs_ = 0;
  weeklyGoalTimeMs_ = 0;
  if (weeklyChartWeekStartDay_ <= 0) {
    const int64_t today = currentStatisticsDay();
    if (today <= 0) return;
    weeklyChartWeekStartDay_ = today - mondayBasedWeekday(today);
  }

  const std::vector<ReadingHistoryEntry>& history = getReadingHistory();

  for (size_t index = 0; index < weeklyReadingTimeMs_.size(); ++index) {
    const uint32_t dateKey = readingDayToDate(weeklyChartWeekStartDay_ + static_cast<int64_t>(index));
    const uint32_t readingMs = readingTimeForDate(history, dateKey);
    weeklyReadingTimeMs_[index] = readingMs;
    weeklyTotalReadingTimeMs_ += readingMs;
  }

  const ReadingGoal::Status goal = ReadingGoal::status();
  if (goal.goalMinutes > 0) {
    weeklyGoalTimeMs_ = static_cast<uint32_t>(goal.goalMinutes) * 7UL * 60UL * 1000UL;
  }
}

void Statistics::refreshMonthlyChartData() {
  monthlyReadingTimeMs_.fill(0);
  monthlyTotalReadingTimeMs_ = 0;
  monthlyGoalTimeMs_ = 0;
  monthlyChartDays_ = 0;
  if (chartPeriodStartDay_ <= 0) return;

  const uint32_t firstDateKey = readingDayToDate(chartPeriodStartDay_);
  const int year = static_cast<int>(firstDateKey / 10000UL);
  const unsigned month = static_cast<unsigned>((firstDateKey / 100UL) % 100UL);
  const int monthDays = daysInMonth(year, month);
  if (monthDays <= 0) return;
  chartPeriodStartDay_ = monthStartDay(chartPeriodStartDay_);
  monthlyChartDays_ = monthDays;

  const std::vector<ReadingHistoryEntry>& history = getReadingHistory();
  for (int index = 0; index < monthlyChartDays_; ++index) {
    const uint32_t dateKey = readingDayToDate(chartPeriodStartDay_ + index);
    const uint32_t readingMs = readingTimeForDate(history, dateKey);
    monthlyReadingTimeMs_[static_cast<size_t>(index)] = readingMs;
    monthlyTotalReadingTimeMs_ += readingMs;
  }

  const ReadingGoal::Status goal = ReadingGoal::status();
  if (goal.goalMinutes > 0) {
    monthlyGoalTimeMs_ = static_cast<uint32_t>(goal.goalMinutes) * static_cast<uint32_t>(monthlyChartDays_) * 60UL *
                         1000UL;
  }
}

bool Statistics::scrollMonthlyChart(const int direction) {
  if (!monthlyChart_ || direction == 0) return false;

  int plottedCount = 0;
  for (int index = 0; index < monthlyChartDays_; ++index) {
    if (monthlyReadingTimeMs_[static_cast<size_t>(index)] > 0) ++plottedCount;
  }

  constexpr int left = 24;
  const int right = renderer.getScreenWidth() - 24;
  const int gridWidth = std::max(1, (right - 8) - (left + 8));
  const int barGap = plottedCount > 20 ? 3 : 6;
  const int maxVisible = std::max(1, (gridWidth + barGap) / (kMonthlyMaxBarWidth + barGap));
  if (plottedCount <= maxVisible) return false;

  const int maxOffset = plottedCount - maxVisible;
  const int nextOffset = std::max(0, std::min(maxOffset, monthlyChartOffset_ + direction));
  if (nextOffset == monthlyChartOffset_) return true;
  monthlyChartOffset_ = nextOffset;
  updateRequired = true;
  renderPage();
  return true;
}

void Statistics::refreshChartData() {
  if (monthlyChart_) {
    refreshMonthlyChartData();
    return;
  }
  weeklyChartWeekStartDay_ = chartPeriodStartDay_;
  refreshWeeklyChartData();
}

void Statistics::moveChartPeriod(const int direction) {
  if (direction == 0) return;
  const int64_t currentDay = currentStatisticsDay();
  if (currentDay <= 0) return;

  if (monthlyChart_) {
    chartPeriodStartDay_ = monthStartDay(chartPeriodStartDay_);
    const int64_t currentMonthStart = monthStartDay(currentDay);
    if (direction > 0) {
      const uint32_t dateKey = readingDayToDate(chartPeriodStartDay_);
      const int year = static_cast<int>(dateKey / 10000UL);
      const unsigned month = static_cast<unsigned>((dateKey / 100UL) % 100UL);
      chartPeriodStartDay_ += daysInMonth(year, month);
    } else {
      chartPeriodStartDay_ = monthStartDay(chartPeriodStartDay_ - 1);
    }
    if (chartPeriodStartDay_ > currentMonthStart) chartPeriodStartDay_ = currentMonthStart;
  } else {
    const int64_t currentWeekStart = currentDay - mondayBasedWeekday(currentDay);
    chartPeriodStartDay_ += static_cast<int64_t>(direction) * 7;
    if (chartPeriodStartDay_ > currentWeekStart) chartPeriodStartDay_ = currentWeekStart;
  }

  monthlyChartOffset_ = 0;
  refreshChartData();
  updateRequired = true;
  renderPage();
}

void Statistics::hydrateFromStorage() {
  if (!loadGlobalStats(globalStats)) {
    globalStats = GlobalReadingStats();
  }
  allBooksStats.clear();
  loadedBookStatsFlags_.clear();
  bookListMetadata_ = loadBookListMetadata();
  bookListMetadata_.erase(
      std::remove_if(bookListMetadata_.begin(), bookListMetadata_.end(), [](const BookListMetadata& book) {
        return !(book.progressPercent > 0.0f);
      }),
      bookListMetadata_.end());

  // Restore the recent book snapshot without scanning the cache directories.
  // This keeps the main page's progress populated while the full statistics
  // rebuild remains an explicit refresh action.
  RECENT_BOOKS.loadFromFile();
  for (const RecentBook& recent : RECENT_BOOKS.getBooks()) {
    std::string cachePath = recent.cachePath;
    if (cachePath.empty() && !recent.path.empty()) {
      cachePath = epubCachePathForBookPath(recent.path);
    }
    if (cachePath.empty()) continue;

    BookReadingStats stats;
    if (!loadBookStats(cachePath.c_str(), stats) || !(stats.progressPercent > 0.0f)) continue;
    stats.path = cachePath;
    if (stats.title.empty()) stats.title = recent.title;
    if (stats.author.empty()) stats.author = recent.author;
    allBooksStats.push_back(std::move(stats));
  }
  std::sort(allBooksStats.begin(), allBooksStats.end(),
            [](const BookReadingStats& left, const BookReadingStats& right) {
              return left.lastReadTimeMs > right.lastReadTimeMs;
            });
  loadedBookStatsFlags_.assign(allBooksStats.size(), 1);
  loadIndexedBooks();
}

void Statistics::loadIndexedBooks() {
  indexedBooks_.clear();
  if (!LibraryIndex::hasIndex() || !LibraryIndex::search("", indexedBooks_, LibraryIndex::all)) {
    indexedBooks_.clear();
    return;
  }
  indexedBooks_.erase(
      std::remove_if(indexedBooks_.begin(), indexedBooks_.end(), [](const LibraryIndex::Book& book) {
        return book.type != LibraryIndex::Book::Type::BOOK;
      }),
      indexedBooks_.end());

  // The library index contains every book on the card, while Statistics only
  // shows books that have actually been started. Use the persisted metadata
  // snapshot here so opening the page remains a read-only, fast operation.
  indexedBooks_.erase(
      std::remove_if(indexedBooks_.begin(), indexedBooks_.end(), [this](const LibraryIndex::Book& book) {
        const std::string cachePath = epubCachePathForBookPath(book.path);
        const std::string thumbnailPath = thumbnailPathForBookCache(cachePath);
        const auto metadata = std::find_if(
            bookListMetadata_.begin(), bookListMetadata_.end(), [&book, &thumbnailPath](const BookListMetadata& item) {
              if (!thumbnailPath.empty() && item.thumbnailPath == thumbnailPath) {
                return true;
              }
              return !book.title.empty() && item.title == book.title;
            });
        return metadata == bookListMetadata_.end() || !(metadata->progressPercent > 0.0f);
      }),
      indexedBooks_.end());
}

bool Statistics::ensureBookStatsLoaded(const int bookIdx) {
  if (bookIdx < 0 || bookIdx >= static_cast<int>(allBooksStats.size())) {
    return false;
  }
  if (bookIdx < static_cast<int>(loadedBookStatsFlags_.size()) &&
      loadedBookStatsFlags_[static_cast<size_t>(bookIdx)] != 0) {
    return true;
  }
  BookReadingStats& slot = allBooksStats[static_cast<size_t>(bookIdx)];
  const std::string cachePath = slot.path;
  BookReadingStats loaded;
  if (!loadBookStats(cachePath.c_str(), loaded)) {
    return false;
  }
  loaded.path = cachePath;
  slot = loaded;
  if (bookIdx < static_cast<int>(loadedBookStatsFlags_.size())) {
    loadedBookStatsFlags_[static_cast<size_t>(bookIdx)] = 1;
  }
  return true;
}

void Statistics::renderCover(const std::string& bookPath, int x, int y, int width, int height,
                                    const std::string& title, const std::string& author) const {
  const std::string coverJpegPath = bookPath + "/thumb.jpg";
  const std::string coverPngPath = bookPath + "/thumb.png";
  const std::string coverBmpPath = bookPath + "/thumb.bmp";
  bool coverDrawn = false;
  ImageRender::Options imageOptions;
  imageOptions.cropToFill = true;
  imageOptions.asyncDisplayCache = true;
  imageOptions.roundedOutside = SETTINGS.bitmapRoundedCorners != 0 ? BitmapRender::RoundedOutside::PaperOutside
                                                                   : BitmapRender::RoundedOutside::None;

  if (SdMan.exists(coverJpegPath.c_str())) {
    coverDrawn = ImageRender::create(renderer, coverJpegPath)
                     .render(x + 2, y + 2, std::max(1, width - 4), std::max(1, height - 4), imageOptions);
  }
  if (!coverDrawn && SdMan.exists(coverPngPath.c_str())) {
    coverDrawn = ImageRender::create(renderer, coverPngPath)
                     .render(x + 2, y + 2, std::max(1, width - 4), std::max(1, height - 4), imageOptions);
  }
  if (!coverDrawn && SdMan.exists(coverBmpPath.c_str())) {
    const int maxW = std::max(1, width - 4);
    const int maxH = std::max(1, height - 4);
    coverDrawn = ImageRender::create(renderer, coverBmpPath).render(x + 2, y + 2, maxW, maxH, imageOptions);
  }

  if (coverDrawn) {
    return;
  }

  const bool rr = SETTINGS.bitmapRoundedCorners != 0;
  renderer.rectangle.fill(x, y, width, height, false, rr);
  if (!rr) {
    renderer.rectangle.dotted(x, y, width, height, true);
  }

  if (!title.empty()) {
    int lineY = y + 18;
    int maxWidth = width - 24;
    int lineHeight = renderer.text.getLineHeight(fontSerifSm());

    std::string remaining = title;
    int lineCount = 0;

    while (!remaining.empty() && lineCount < 3) {
      std::string line;
      int lineWidth = 0;

      while (!remaining.empty()) {
        size_t spacePos = remaining.find(' ');
        std::string word = (spacePos != std::string::npos) ? remaining.substr(0, spacePos) : remaining;

        int wordWidth = renderer.text.getWidth(fontSerifSm(), word.c_str(), EpdFontFamily::REGULAR);

        if (lineWidth + wordWidth <= maxWidth) {
          if (!line.empty()) line += " ";
          line += word;
          lineWidth += wordWidth;

          if (spacePos != std::string::npos) {
            remaining = remaining.substr(spacePos + 1);
          } else {
            remaining.clear();
          }
        } else {
          break;
        }
      }

      if (line.empty()) {
        break;
      }

      int textWidth = renderer.text.getWidth(fontSerifSm(), line.c_str(), EpdFontFamily::REGULAR);
      int textX = x + (width - textWidth) / 2;
      renderer.text.render(fontSerifSm(), textX, lineY, line.c_str(), true, EpdFontFamily::REGULAR);
      lineY += lineHeight;
      lineCount++;
    }
  }

  if (!author.empty()) {
    std::string authorText = author;
    int authorWidth = renderer.text.getWidth(systemFontId(), authorText.c_str());
    int authorX = x + (width - authorWidth) / 2;
    int authorY = y + height - 22;
    renderer.text.render(systemFontId(), authorX, authorY, authorText.c_str());
  }
}

std::pair<int, int> Statistics::drawGlobalRecentThumbBlock(int boxX, int yTop, const std::string& bookPath,
                                                                  const std::string& title) const {
  constexpr int kMaxBoxW = 165;
  constexpr int kMaxBoxH = 182;
  constexpr int kOuterPad = 2;
  const int availW = std::max(1, kMaxBoxW - 4);
  const int availH = std::max(1, kMaxBoxH - 4);

  const std::string coverJpegPath = bookPath + "/thumb.jpg";
  const std::string coverPngPath = bookPath + "/thumb.png";
  const std::string coverBmpPath = bookPath + "/thumb.bmp";

  if (SdMan.exists(coverJpegPath.c_str())) {
    const int coverW = availW + 4;
    const int coverH = availH + 4;
    const int frameW = coverW + 2 * kOuterPad;
    const int frameH = coverH + 2 * kOuterPad;
    const int innerX = boxX + kOuterPad;
    const int innerY = yTop + kOuterPad;
    const bool rr = SETTINGS.bitmapRoundedCorners != 0;
    renderer.rectangle.fill(boxX, yTop, frameW, frameH, false, rr);
    if (!rr) {
      renderer.rectangle.dotted(boxX, yTop, frameW, frameH, true);
    }
    ImageRender::Options imageOptions;
    imageOptions.cropToFill = true;
    if (ImageRender::create(renderer, coverJpegPath).render(innerX + 2, innerY + 2, availW, availH, imageOptions)) {
      return {frameW, frameH};
    }
  }

  if (SdMan.exists(coverPngPath.c_str())) {
    const int coverW = availW + 4;
    const int coverH = availH + 4;
    const int frameW = coverW + 2 * kOuterPad;
    const int frameH = coverH + 2 * kOuterPad;
    const int innerX = boxX + kOuterPad;
    const int innerY = yTop + kOuterPad;
    const bool rr = SETTINGS.bitmapRoundedCorners != 0;
    renderer.rectangle.fill(boxX, yTop, frameW, frameH, false, rr);
    if (!rr) {
      renderer.rectangle.dotted(boxX, yTop, frameW, frameH, true);
    }
    ImageRender::Options imageOptions;
    imageOptions.cropToFill = true;
    if (ImageRender::create(renderer, coverPngPath).render(innerX + 2, innerY + 2, availW, availH, imageOptions)) {
      return {frameW, frameH};
    }
  }

  if (SdMan.exists(coverBmpPath.c_str())) {
    const int coverW = availW + 4;
    const int coverH = availH + 4;
    const int frameW = coverW + 2 * kOuterPad;
    const int frameH = coverH + 2 * kOuterPad;
    const int innerX = boxX + kOuterPad;
    const int innerY = yTop + kOuterPad;
    const bool rr = SETTINGS.bitmapRoundedCorners != 0;
    renderer.rectangle.fill(boxX, yTop, frameW, frameH, false, rr);
    if (!rr) {
      renderer.rectangle.dotted(boxX, yTop, frameW, frameH, true);
    }
    ImageRender::Options imageOptions;
    imageOptions.cropToFill = true;
    imageOptions.roundedOutside = SETTINGS.bitmapRoundedCorners != 0 ? BitmapRender::RoundedOutside::PaperOutside
                                                                     : BitmapRender::RoundedOutside::None;
    if (ImageRender::create(renderer, coverBmpPath).render(innerX + 2, innerY + 2, availW, availH, imageOptions)) {
      return {frameW, frameH};
    }
  }

  constexpr int kFallbackW = 120;
  constexpr int kFallbackH = 132;
  const int coverW = kFallbackW + 4;
  const int coverH = kFallbackH + 4;
  const int frameW = coverW + 2 * kOuterPad;
  const int frameH = coverH + 2 * kOuterPad;
  const bool rr = SETTINGS.bitmapRoundedCorners != 0;
  renderer.rectangle.fill(boxX, yTop, frameW, frameH, false, rr);
  if (!rr) {
    renderer.rectangle.dotted(boxX, yTop, frameW, frameH, true);
  }
  renderCover(bookPath, boxX + kOuterPad, yTop + kOuterPad, coverW, coverH, title, "");
  return {frameW, frameH};
}

void Statistics::onEnter() {
  UiPage::onEnter();

  hydrateFromStorage();
  viewIndex = 0;
  statsTab_ = 0;
  controlDrawerOpen_ = false;
  indexRefreshPending_ = false;

  uint32_t calendarDate = 0;
  currentRtcDateKey(calendarDate);
  if (calendarDate == 0) {
    const std::vector<ReadingHistoryEntry>& history = getReadingHistory();
    if (!history.empty()) calendarDate = history.back().dateKey;
  }
  calendarWeekStartDay_ = calendarDate != 0
                              ? readingDateToDay(calendarDate) - mondayBasedWeekday(readingDateToDay(calendarDate))
                              : 0;
  monthlyChart_ = false;
  monthlyChartOffset_ = 0;
  chartPeriodStartDay_ = calendarWeekStartDay_;
  weeklyChartWeekStartDay_ = calendarWeekStartDay_;
  refreshChartData();
  SETTINGS.runHalfRefreshOnLoadIfEnabled(renderer, SystemSetting::RefreshOnLoadPage::Statistics);
}

void Statistics::onExit() {
  UiPage::onExit();

  std::vector<BookReadingStats>().swap(allBooksStats);
  std::vector<uint8_t>().swap(loadedBookStatsFlags_);
  std::vector<BookListMetadata>().swap(bookListMetadata_);
  std::vector<LibraryIndex::Book>().swap(indexedBooks_);
}

void Statistics::refresh() {
  if (LibraryIndexRefresh::isRunning()) return;
  // Rebuild the persisted per-book snapshot only on an explicit refresh.
  // Opening Statistics uses the already saved snapshot and library index.
  loadStats(false);
  indexRefreshPending_ = true;
  LibraryIndexRefresh::start(renderer, this, "Loading statistics...");
}

void Statistics::menu() {
  if (statsTab_ != 0) {
    const char* pageTitle = statsTab_ == 1 ? "Week" : (statsTab_ == 2 ? "Books" : "Book");
    SubPage::header(renderer, pageTitle);
    drawStatisticsRefreshButton(renderer);
    return;
  }
  title();
  drawStatisticsRefreshButton(renderer);
  renderer.bitmap.icon(Close, renderer.getScreenWidth() - 60, navigation::Menu::topPadding, 40, 40);
  if (controlDrawerOpen_) drawControlDrawer();
}

void Statistics::title() const {
  renderer.bitmap.icon(Hamburger, navigation::Menu::leftMargin, navigation::Menu::topPadding,
                       navigation::Menu::iconSize, navigation::Menu::iconSize);
  const int font = systemFontIdForPointSize(16);
  const int textY = navigation::Menu::topPadding +
                    (navigation::Menu::iconSize - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, navigation::Menu::leftMargin + navigation::Menu::iconSize + 12, textY,
                       "Statistics", true, EpdFontFamily::BOLD);
}

void Statistics::drawControlDrawer() const {
  constexpr int itemCount = static_cast<int>(sizeof(kStatisticsActions) / sizeof(kStatisticsActions[0]));
  constexpr int topPadding = 24;
  constexpr int rowGap = 8;
  constexpr int iconSize = 40;
  const int listTop = Sidebar::listTop();
  const int rowHeight = Sidebar::rowHeight;
  const int font = systemFontId();
  const int lineHeight = renderer.text.getLineHeight(font);

  Sidebar::renderFrame(renderer, "Statistics");
  for (int index = 0; index < itemCount; ++index) {
    const int rowY = listTop + topPadding + index * (rowHeight + rowGap);
    const int iconX = 24;
    const int iconY = rowY + (rowHeight - iconSize) / 2;
    renderer.bitmap.icon(kStatisticsActions[index].icon, iconX, iconY, iconSize, iconSize);
    renderer.text.render(font, iconX + iconSize + 16, rowY + (rowHeight - lineHeight) / 2,
                         kStatisticsActions[index].label, true);
  }
}

bool Statistics::handleHamburgerTap() {
  if (statsTab_ != 0 || controlDrawerOpen_ || !mappedInput.hasTouch()) return false;
  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) return false;
  const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
  if (!navigation::Menu::hamburgerHit(tapX, tapY)) {
    mappedInput.restoreTouchTapInScreen(renderer, tapNx, tapNy);
    return false;
  }
  controlDrawerOpen_ = true;
  updateRequired = true;
  renderPage();
  return true;
}

bool Statistics::handleRefreshTap() {
  if (!mappedInput.hasTouch()) {
    return false;
  }

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
    return false;
  }

  const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
  constexpr int touchPadding = 8;
  const ButtonBounds refreshBounds = statisticsRefreshButton(renderer);
  if (tapX >= refreshBounds.x - touchPadding && tapX < refreshBounds.x + refreshBounds.width + touchPadding &&
      tapY >= refreshBounds.y - touchPadding && tapY < refreshBounds.y + refreshBounds.height + touchPadding) {
    refresh();
    return true;
  }

  mappedInput.restoreTouchTapInScreen(renderer, tapNx, tapNy);
  return false;
}

bool Statistics::handleChartPeriodToggleTap() {
  if (statsTab_ != 0 || !mappedInput.hasTouch()) return false;

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) return false;

  const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
  const ButtonBounds bounds = statisticsChartPeriodButton(renderer, kWeeklyChartTop);
  constexpr int touchPadding = 8;
  if (tapX < bounds.x - touchPadding || tapX >= bounds.x + bounds.width + touchPadding ||
      tapY < bounds.y - touchPadding || tapY >= bounds.y + bounds.height + touchPadding) {
    mappedInput.restoreTouchTapInScreen(renderer, tapNx, tapNy);
    return false;
  }

  const int64_t anchorDay = monthlyChart_ ? chartPeriodStartDay_ : chartPeriodStartDay_ + 3;
  monthlyChart_ = !monthlyChart_;
  monthlyChartOffset_ = 0;
  chartPeriodStartDay_ = monthlyChart_ ? monthStartDay(anchorDay)
                                       : anchorDay - mondayBasedWeekday(anchorDay);
  refreshChartData();
  updateRequired = true;
  renderPage();
  return true;
}

bool Statistics::handleControlDrawerInput() {
  if (!controlDrawerOpen_) return false;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      (mappedInput.hasTouch() && mappedInput.wasTouchSwipeRight())) {
    controlDrawerOpen_ = false;
    updateRequired = true;
    renderPage();
    return true;
  }
  if (!mappedInput.hasTouch()) return true;

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) return true;
  const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
  const int drawerWidth = Sidebar::width(renderer);
  if (tapX >= drawerWidth) {
    controlDrawerOpen_ = false;
    updateRequired = true;
    renderPage();
    return true;
  }

  constexpr int itemCount = static_cast<int>(sizeof(kStatisticsActions) / sizeof(kStatisticsActions[0]));
  const int item = Sidebar::hitTest(renderer, tapX, tapY, itemCount, 0);
  if (item < 0) return true;

  controlDrawerOpen_ = false;
  if (item == 0) {
    statsTab_ = 1;
    viewIndex = 0;
  } else if (item == 1) {
    statsTab_ = 0;
    viewIndex = 0;
  } else if (item == 2) {
    statsTab_ = 2;
    selectedBookIndex_ = -1;
    booksPage_ = 0;
  }
  updateRequired = true;
  renderPage();
  return true;
}

bool Statistics::handleBooksInput() {
  if (statsTab_ != 2) {
    return false;
  }

  const auto pageCount = [this] {
    const int bookCount = indexedBooks_.empty() ? static_cast<int>(bookListMetadata_.size())
                                                : static_cast<int>(indexedBooks_.size());
    return std::max(1, (bookCount + kBooksPerPage - 1) / kBooksPerPage);
  };
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    booksPage_ = std::max(0, booksPage_ - 1);
    updateRequired = true;
    renderPage();
    return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    booksPage_ = std::min(pageCount() - 1, booksPage_ + 1);
    updateRequired = true;
    renderPage();
    return true;
  }
  if (!mappedInput.hasTouch()) {
    return false;
  }

  constexpr int rowHeight = kBooksRowHeight;
  const int contentTop = kStatsContentTop;
  const int contentBottom = renderer.getScreenHeight() - 12;

  if (mappedInput.wasTouchSwipeUp()) {
    booksPage_ = std::min(pageCount() - 1, booksPage_ + 1);
    updateRequired = true;
    renderPage();
    return true;
  }
  if (mappedInput.wasTouchSwipeDown()) {
    booksPage_ = std::max(0, booksPage_ - 1);
    updateRequired = true;
    renderPage();
    return true;
  }

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
    return false;
  }
  const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
  if (tapY >= contentTop && tapY < contentBottom) {
    const int row = (tapY - contentTop) / rowHeight;
    const int index = booksPage_ * kBooksPerPage + row;
    const int rowY = contentTop + row * rowHeight;
    const int bookCount = indexedBooks_.empty() ? static_cast<int>(bookListMetadata_.size())
                                                : static_cast<int>(indexedBooks_.size());
    if (index >= 0 && index < bookCount && rowY >= contentTop &&
        rowY + rowHeight <= contentBottom) {
      if (!indexedBooks_.empty()) {
        const std::string cachePath = epubCachePathForBookPath(indexedBooks_[static_cast<size_t>(index)].path);
        auto existing = std::find_if(allBooksStats.begin(), allBooksStats.end(), [&cachePath](const BookReadingStats& book) {
          return book.path == cachePath;
        });
        if (existing == allBooksStats.end()) {
          BookReadingStats loaded;
          if (!loadBookStats(cachePath.c_str(), loaded)) {
            loaded.path = cachePath;
            loaded.title = indexedBooks_[static_cast<size_t>(index)].title;
          }
          loaded.path = cachePath;
          allBooksStats.push_back(std::move(loaded));
          loadedBookStatsFlags_.push_back(1);
          selectedBookIndex_ = static_cast<int>(allBooksStats.size()) - 1;
        } else {
          selectedBookIndex_ = static_cast<int>(std::distance(allBooksStats.begin(), existing));
        }
      } else {
        selectedBookIndex_ = index;
      }
      statsTab_ = 3;
      updateRequired = true;
      renderPage();
      return true;
    }
  }

  mappedInput.restoreTouchTapInScreen(renderer, tapNx, tapNy);
  return false;
}

int Statistics::renderRecent(int y, int innerLeft, int innerRight, int innerW, int Margin) const {
  constexpr int kThumbTextGap = 15;
  constexpr int kGlobalThumbOuterPad = 2;
  constexpr int g8 = 8;
  constexpr int g10 = 10;

  const int lhSerif = renderer.text.getLineHeight(fontSerif());
  const int lhSans = renderer.text.getLineHeight(systemFontId());
  const int lhSm = renderer.text.getLineHeight(fontSansSm());
  const int yCoverTop = y;

  std::pair<int, int> tf;
  int yThumbBottom;
  if (!allBooksStats.empty()) {
    const BookReadingStats& cur = allBooksStats[0];
    const float prog = (cur.progressPercent >= 0.f) ? std::min(1.f, std::max(0.f, cur.progressPercent / 100.f)) : 0.f;
    tf = drawGlobalRecentThumbBlock(innerLeft, yCoverTop, cur.path, cur.title);
    const int textX = innerLeft + tf.first + kThumbTextGap;
    const int textColW = std::max(40, innerRight - textX);

    const int yTitle = yCoverTop + kGlobalThumbOuterPad;
    std::string titleLine = renderer.text.truncate(fontSerif(), cur.title.c_str(), textColW, EpdFontFamily::ITALIC);
    renderer.text.render(fontSerif(), textX, yTitle, titleLine.c_str(), true, EpdFontFamily::ITALIC);
    const int yAuthor = yTitle + lhSerif + g8;
    if (!cur.author.empty()) {
      std::string auth = renderer.text.truncate(systemFontId(), cur.author.c_str(), textColW);
      renderer.text.render(systemFontId(), textX, yAuthor, auth.c_str());
    }
    const int yProg = yAuthor + lhSans + g10;
    char progLabel[48];
    snprintf(progLabel, sizeof(progLabel), "Book progress: %.0f%%", prog * 100.f);
    renderer.text.render(fontSansSm(), textX, yProg, progLabel);
    const int yBar = yProg + lhSm + g8;
    drawThinProgressBar(renderer, textX, yBar, textColW, 8, prog);
    yThumbBottom = yBar + 8;
  } else {
    tf = drawGlobalRecentThumbBlock(innerLeft, yCoverTop, "", "");
    const int nw = renderer.text.getWidth(systemFontId(), "No recent book");
    const int yEmpty = yCoverTop + (tf.second - lhSans) / 2;
    renderer.text.render(systemFontId(), innerLeft + (tf.first - nw) / 2, yEmpty, "No recent book");
    yThumbBottom = yCoverTop + tf.second;
  }

  const int thumbHeightPx = std::max(tf.second, yThumbBottom - yCoverTop);
  return yCoverTop + thumbHeightPx + Margin;
}

int Statistics::renderCalendar(const int y, const int innerLeft, const int innerW, const int contentBottom) const {
  if (calendarWeekStartDay_ <= 0 || innerW <= 0 || contentBottom <= y) return 0;

  const int smallFont = fontSansSm();
  const int smallLine = renderer.text.getLineHeight(smallFont);
  const int gridTop = y;
  const int gridBottom = contentBottom;
  if (gridBottom <= gridTop) return 0;
  const int cellWidth = innerW;
  const int cellHeight = std::max(1, (gridBottom - gridTop - kCalendarGap * 6) / 7);

  constexpr const char* weekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  std::array<std::vector<std::string>, 7> dayBooks;
  std::array<uint32_t, 7> dayTimes{};
  const std::vector<ReadingHistoryEntry>& history = getReadingHistory();
  const std::vector<ReadingHistoryBookEntry>& bookHistory = getReadingHistoryBooks();

  for (int column = 0; column < 7; ++column) {
    const uint32_t dateKey = readingDayToDate(calendarWeekStartDay_ + column);
    dayTimes[static_cast<size_t>(column)] = readingTimeForDate(history, dateKey);
    for (const ReadingHistoryBookEntry& entry : bookHistory) {
      if (entry.dateKey != dateKey || entry.bookPath.empty()) continue;
      auto& books = dayBooks[static_cast<size_t>(column)];
      if (std::find(books.begin(), books.end(), entry.bookPath) == books.end()) {
        books.push_back(entry.bookPath);
      }
    }
  }

  // Leave enough room for the weekday and two-digit date without truncating
  // labels such as "Wed 24" before the thumbnails begin.
  constexpr int kDayLabelWidth = 90;
  constexpr int kDailyTotalWidth = 78;
  const int totalFont = fontSerifSm();
  const int totalLine = renderer.text.getLineHeight(totalFont);
  for (int day = 0; day < 7; ++day) {
    const int cellY = gridTop + day * (cellHeight + kCalendarGap);
    renderer.rectangle.fill(innerLeft, cellY, cellWidth, cellHeight,
                            static_cast<int>(GfxRenderer::FillTone::Paper), false);

    char dayLabel[24];
    const uint32_t dateKey = readingDayToDate(calendarWeekStartDay_ + day);
    snprintf(dayLabel, sizeof(dayLabel), "%s %02lu", weekdays[day],
             static_cast<unsigned long>(dateKey % 100UL));
    const int dayLabelY = cellY + (cellHeight - smallLine) / 2;
    renderer.text.render(smallFont, innerLeft + 4, dayLabelY, dayLabel, true, EpdFontFamily::BOLD);

    const auto& books = dayBooks[static_cast<size_t>(day)];
    const uint32_t readingTime = dayTimes[static_cast<size_t>(day)];
    const int thumbX = innerLeft + kDayLabelWidth;
    const int thumbWidthArea = std::max(1, cellWidth - kDayLabelWidth - kDailyTotalWidth - 8);
    const int thumbHeight = cellHeight;
    const int thumbWidth = std::min(66, std::max(48, (thumbHeight * 4) / 5));
    const int thumbGap = 10;
    const int maxThumbs = std::max(1, (thumbWidthArea + thumbGap) / (thumbWidth + thumbGap));
    const int thumbsToDraw = std::min(static_cast<int>(books.size()), maxThumbs);
    const int thumbTop = cellY;
    for (int index = 0; index < thumbsToDraw; ++index) {
      const int x = thumbX + index * (thumbWidth + thumbGap);
      renderCover(books[static_cast<size_t>(index)], x, thumbTop, thumbWidth, thumbHeight, "", "");
    }

    char totalLabel[16];
    const double hours = static_cast<double>(readingTime) / 3600000.0;
    if (std::fabs(hours - std::round(hours)) < 0.05) {
      std::snprintf(totalLabel, sizeof(totalLabel), "%.0fh", hours);
    } else {
      std::snprintf(totalLabel, sizeof(totalLabel), "%.1fh", hours);
    }
    const int totalWidth = renderer.text.getWidth(totalFont, totalLabel, EpdFontFamily::BOLD);
    const int totalX = innerLeft + cellWidth - totalWidth - 4;
    const int totalY = cellY + (cellHeight - totalLine) / 2;
    renderer.text.render(totalFont, totalX, totalY, totalLabel, true, EpdFontFamily::BOLD);

    if (day < 6) {
      const int dividerY = cellY + cellHeight + (kCalendarGap / 2);
      renderer.line.render(innerLeft + 4, dividerY, innerLeft + cellWidth - 4, dividerY, true,
                           LineRender::Style::Dotted);
    }
  }

  return 0;
}

int Statistics::renderFirstGrid(int y, int innerLeft, int innerW, int Margin) const {
  constexpr int kStatsRowH = 58;
  char v0[20], v1[20], v2[20], v3[20];
  const float totalHrs = static_cast<float>(globalStats.totalReadingTimeMs) / 3600000.f;
  snprintf(v0, sizeof(v0), "%.1f", totalHrs);
  const float avgMinPerSess =
      globalStats.totalSessions > 0
          ? static_cast<float>(globalStats.totalReadingTimeMs) / 60000.f / static_cast<float>(globalStats.totalSessions)
          : 0.f;
  snprintf(v1, sizeof(v1), "%.0f", avgMinPerSess);
  snprintf(v2, sizeof(v2), "%u", globalStats.totalPagesRead);
  const float readMin = static_cast<float>(globalStats.totalReadingTimeMs) / 60000.f;
  const float pgPerMin = readMin > 0.01f ? static_cast<float>(globalStats.totalPagesRead) / readMin : 0.f;
  snprintf(v3, sizeof(v3), "%.1f", pgPerMin);

  drawFourColumnStats2x2(renderer, innerLeft, y, innerW, v0, "Total hours", v1, "Avg. min/session", v2,
                         "Pages read", v3, "Pages per min", kStatsRowH, 0);
  return y + kStatsRowH * 2 + 20 + Margin;
}

int Statistics::renderGuage(int y, int innerLeft, int innerRight, int Margin) const {
  const GlobalAllItemsGeom g = computeGlobalAllItemsGeom(renderer);
  float ratio = 0.f;
  if (globalStats.totalBooksStarted > 0) {
    ratio = std::min(
        1.f, static_cast<float>(globalStats.totalBooksFinished) / static_cast<float>(globalStats.totalBooksStarted));
  }
  drawGlobalAllItemsGaugeRow(renderer, innerLeft, innerRight, y, ratio, g);
  return y + g.rowH + Margin;
}

void Statistics::renderSecondGrid(int y, int innerLeft, int innerRight, int contentBottom) const {
  const GlobalAllItemsGeom g = computeGlobalAllItemsGeom(renderer);
  drawGlobalAllItemsSecondBand(renderer, innerLeft, innerRight, y, y, contentBottom, globalStats.totalBooksFinished,
                               globalStats.totalBooksStarted, g);
}

void Statistics::renderSingleBookView(int bookIdx, int contentTop, int contentBottom) const {
  if (bookIdx < 0 || bookIdx >= static_cast<int>(allBooksStats.size())) {
    return;
  }
  const BookReadingStats& b = allBooksStats[static_cast<size_t>(bookIdx)];
  constexpr int kMarginX = 30;
  constexpr int g8 = 8;
  constexpr int g10 = 10;
  /** Taller rows than global stats; extra tail height so the pages row sits below the mid divider cleanly. */
  constexpr int kSingleBookStatsRowH = UiLayout::LIST_ITEM_HEIGHT;
  constexpr int kSingleBookStatsLastRowExtraPx = 18;

  const int innerLeft = kMarginX;
  const int innerRight = renderer.getScreenWidth() - kMarginX;
  const int innerW = innerRight - innerLeft;
  const int y0 = contentTop;
  const int yEnd = contentBottom - 24;

  const int lhSerif = renderer.text.getLineHeight(fontSerif());
  const int lhSans = renderer.text.getLineHeight(systemFontId());
  /** Title and author below cover row; sessions/chapters are in the bottom stats grid (same style as hours). */
  const int metaSpan = lhSerif + g8 + lhSans + g10;
  constexpr int gapCoverTitle = 6;
  constexpr int gapMetaStats = 12;
  const int hStats = kSingleBookStatsRowH * 3 + kSingleBookStatsLastRowExtraPx;
  constexpr int kSingleBookStatsGridLiftPx = 20;
  const int yStatsTop = yEnd - hStats - kSingleBookStatsGridLiftPx;
  const int maxTitleY = yStatsTop - gapMetaStats - metaSpan;

  int y = y0 + g8;
  const int yCoverTop = y;

  /** Donut anchored toward the right margin with a wide gap from the cover. */
  constexpr int kBookDonutR = 76;
  constexpr int kBookDonutThick = 11;
  constexpr int kCoverGaugeGap = 32;
  constexpr int kGaugeRightMargin = 55;
  const int cxGauge = innerRight - kGaugeRightMargin - kBookDonutR;
  const int maxCoverW = std::max(100, cxGauge - kBookDonutR - kCoverGaugeGap - innerLeft);

  const int coverAllow = std::max(0, maxTitleY - gapCoverTitle - yCoverTop);
  int coverH = std::max(std::min(260, coverAllow), std::min(120, coverAllow));
  int coverW = std::min(maxCoverW, (coverH * 2) / 3);
  if (coverW < 100) {
    coverW = 100;
  }
  coverH = std::min(coverH, (coverW * 3) / 2);
  coverH = std::min(coverH, coverAllow);

  const float prog = (b.progressPercent >= 0.f) ? std::min(1.f, std::max(0.f, b.progressPercent / 100.f)) : 0.f;
  const int boxX = innerLeft;
  const bool rr = SETTINGS.bitmapRoundedCorners != 0;
  renderer.rectangle.fill(boxX, yCoverTop, coverW, coverH, false, rr);
  if (!rr) {
    renderer.rectangle.dotted(boxX, yCoverTop, coverW, coverH, true);
  }
  renderCover(b.path, boxX + 1, yCoverTop + 1, coverW - 2, coverH - 2, b.title, "");

  const int rowHeight = std::max(coverH, 2 * kBookDonutR + 4);
  const int cyGauge = yCoverTop + rowHeight / 2;
  char pctStr[16];
  snprintf(pctStr, sizeof(pctStr), "%.0f%%", prog * 100.f);
  drawFullDonutGauge(renderer, cxGauge, cyGauge, kBookDonutR, kBookDonutThick, prog, pctStr);

  const int textMaxW = innerW;
  std::string titleLine = renderer.text.truncate(fontSerif(), b.title.c_str(), textMaxW, EpdFontFamily::ITALIC);
  const int yTitle = yCoverTop + rowHeight + gapCoverTitle;
  renderer.text.render(fontSerif(), innerLeft, yTitle, titleLine.c_str(), true, EpdFontFamily::ITALIC);
  const int yAuthor = yTitle + lhSerif + g8;
  if (!b.author.empty()) {
    std::string auth = renderer.text.truncate(systemFontId(), b.author.c_str(), textMaxW);
    renderer.text.render(systemFontId(), innerLeft, yAuthor, auth.c_str());
  }

  char v0[20], v1[20], v2[20], v3[20], vSess[16], vChap[16];
  const float bookHrs = static_cast<float>(b.totalReadingTimeMs) / 3600000.f;
  snprintf(v0, sizeof(v0), "%.1f", bookHrs);
  const float bookAvgMin = b.sessionCount > 0
                               ? static_cast<float>(b.totalReadingTimeMs) / 60000.f / static_cast<float>(b.sessionCount)
                               : 0.f;
  snprintf(v1, sizeof(v1), "%.0f", bookAvgMin);
  snprintf(v2, sizeof(v2), "%u", b.totalPagesRead);
  const float bookReadMin = static_cast<float>(b.totalReadingTimeMs) / 60000.f;
  const float bookPgPerMin = bookReadMin > 0.01f ? static_cast<float>(b.totalPagesRead) / bookReadMin : 0.f;
  snprintf(v3, sizeof(v3), "%.1f", bookPgPerMin);
  snprintf(vSess, sizeof(vSess), "%u", static_cast<unsigned>(b.sessionCount));
  snprintf(vChap, sizeof(vChap), "%u", static_cast<unsigned>(b.totalChaptersRead));

  const char* vals[] = {v0, v1, vSess, vChap, v2, v3};
  const char* labs[] = {"Total hours", "Avg. min/session", "Sessions", "Chapters read", "Pages read", "Pages per min"};
  drawFourColumnStatsNx2(renderer, innerLeft, yStatsTop + 10, innerW, vals, labs, 3, kSingleBookStatsRowH, 28,
                         kSingleBookStatsLastRowExtraPx);

  char footer[24];
  snprintf(footer, sizeof(footer), "%d/%zu", bookIdx + 1, allBooksStats.size());
  renderer.text.centered(fontSansSm(), contentBottom - 5, footer);
}

void Statistics::renderSingleBookTopSection(const int bookIdx) const {
  if (bookIdx < 0 || bookIdx >= static_cast<int>(allBooksStats.size())) {
    return;
  }

  const BookReadingStats& book = allBooksStats[static_cast<size_t>(bookIdx)];
  constexpr int kPanelPadding = 20;
  const SingleBookLayout layout = singleBookLayout(renderer);
  const int panelW = renderer.getScreenWidth();
  if (panelW <= 0 || layout.topH <= 0) return;

  renderer.rectangle.fill(0, layout.areaY, panelW, layout.topH, false);

  const int coverH = std::max(1, layout.topH - kPanelPadding * 2);
  const int coverW = std::max(140, std::min(180, panelW * 30 / 100));
  renderCover(book.path, kPanelPadding, layout.areaY + kPanelPadding, coverW, coverH, book.title, book.author);

  const int radialLeft = kPanelPadding + coverW + 28;
  const int radialRight = panelW - kPanelPadding;
  const int radialWidth = std::max(1, radialRight - radialLeft);
  const int radius = std::max(20, std::min(radialWidth / 2 - 8, layout.topH / 2 - 12));
  const int centerX = radialLeft + radialWidth / 2;
  const int centerY = layout.areaY + layout.topH / 2;
  const float progress = book.progressPercent >= 0.0f
                             ? std::min(1.0f, std::max(0.0f, book.progressPercent / 100.0f))
                             : 0.0f;
  char percentText[16];
  std::snprintf(percentText, sizeof(percentText), "%.0f%%", progress * 100.0f);
  drawFullDonutGauge(renderer, centerX, centerY, radius, std::max(8, radius / 8), progress, percentText);
}

void Statistics::renderSingleBookMiddleChart(const int bookIdx) const {
  if (bookIdx < 0 || bookIdx >= static_cast<int>(allBooksStats.size())) {
    return;
  }

  const BookReadingStats& book = allBooksStats[static_cast<size_t>(bookIdx)];
  constexpr int kPanelPaddingX = 20;
  constexpr int kSummaryTopPadding = 6;
  constexpr int kSummaryChartGap = 6;
  constexpr int kChartBottomLabelHeight = 30;
  constexpr int kMiddleBottomPadding = 20;
  const SingleBookLayout layout = singleBookLayout(renderer);
  const int panelX = 0;
  const int panelW = renderer.getScreenWidth();
  const int panelY = layout.areaY + layout.topH;
  const int panelBottom = panelY + layout.middleH;
  if (panelW <= 0 || layout.middleH <= 0) return;

  renderer.rectangle.fill(panelX, panelY, panelW, layout.middleH, false);

  struct Point {
    uint32_t dateKey;
    uint32_t readingTimeMs;
  };
  std::vector<Point> points;
  const std::vector<ReadingHistoryBookEntry>& history = getReadingHistoryBooks();
  for (const ReadingHistoryBookEntry& entry : history) {
    if (entry.bookPath != book.path || entry.dateKey == 0 || entry.readingTimeMs == 0) continue;
    auto existing = std::find_if(points.begin(), points.end(), [&entry](const Point& point) {
      return point.dateKey == entry.dateKey;
    });
    if (existing == points.end()) {
      points.push_back({entry.dateKey, entry.readingTimeMs});
    } else {
      const uint32_t room = UINT32_MAX - existing->readingTimeMs;
      existing->readingTimeMs += std::min(room, entry.readingTimeMs);
    }
  }

  // Keep the most recent dates readable on the compact 1x3 chart while retaining
  // the chronological order stored by the history manager.
  constexpr size_t kMaxChartPoints = 14;
  if (points.size() > kMaxChartPoints) {
    points.erase(points.begin(), points.begin() + static_cast<int>(points.size() - kMaxChartPoints));
  }

  const int valueFont = fontSerifLg();
  const int labelFont = fontSansSm();
  const int valueLine = renderer.text.getLineHeight(valueFont);
  const int labelLine = renderer.text.getLineHeight(labelFont);
  const int summaryY = panelY + kSummaryTopPadding;
  const int summaryValueY = summaryY;
  const int summaryLabelY = summaryValueY + valueLine + 1;
  const int halfW = panelW / 2;

  char totalText[24];
  const float totalHours = static_cast<float>(book.totalReadingTimeMs) / 3600000.0f;
  std::snprintf(totalText, sizeof(totalText), "%.1fh", totalHours);
  const float averageMinutes = book.sessionCount > 0
                                   ? static_cast<float>(book.totalReadingTimeMs) / 60000.0f /
                                         static_cast<float>(book.sessionCount)
                                   : 0.0f;
  char averageText[24];
  std::snprintf(averageText, sizeof(averageText), "%.0fm", averageMinutes);

  renderer.text.render(valueFont, kPanelPaddingX, summaryValueY, totalText, true, EpdFontFamily::BOLD);
  renderer.text.renderGray(labelFont, kPanelPaddingX, summaryLabelY, "Total Hours", true,
                           EpdFontFamily::REGULAR);
  const int averageWidth = renderer.text.getWidth(valueFont, averageText, EpdFontFamily::BOLD);
  const int averageX = halfW + (panelW - halfW - averageWidth) / 2;
  renderer.text.render(valueFont, averageX, summaryValueY, averageText, true, EpdFontFamily::BOLD);
  const char* averageLabel = "Average / Session";
  const int averageLabelWidth = renderer.text.getWidth(labelFont, averageLabel);
  renderer.text.renderGray(labelFont, halfW + (panelW - halfW - averageLabelWidth) / 2, summaryLabelY,
                           averageLabel, true, EpdFontFamily::REGULAR);

  const int plotTop = summaryLabelY + labelLine + kSummaryChartGap;
  const int contentBottom = panelBottom - kMiddleBottomPadding;
  const int plotBottom = contentBottom - kChartBottomLabelHeight;
  if (plotBottom <= plotTop + 8) return;

  const int gridLeft = kPanelPaddingX;
  const int gridRight = panelW - kPanelPaddingX;
  const int plotWidth = std::max(1, gridRight - gridLeft);
  const int plotHeight = plotBottom - plotTop;
  uint32_t maximum = 0;
  for (const Point& point : points) {
    maximum = std::max(maximum, point.readingTimeMs);
  }
  if (maximum == 0) maximum = 1;

  for (int level = 0; level <= 2; ++level) {
    const int lineY = plotBottom - (plotHeight * level) / 2;
    renderer.line.render(gridLeft, lineY, gridRight, lineY, true, LineRender::Style::Dotted);
  }

  if (points.empty()) {
    renderer.text.renderGray(labelFont, gridLeft, plotTop + std::max(0, plotHeight - labelLine) / 2,
                             "No reading history", true, EpdFontFamily::REGULAR);
    return;
  }

  const int pointCount = static_cast<int>(points.size());
  const int pointStep = pointCount > 1 ? plotWidth / (pointCount - 1) : 0;
  std::vector<int> pointYs;
  pointYs.reserve(points.size());
  for (int index = 0; index < pointCount; ++index) {
    const int pointX = pointCount > 1 ? gridLeft + index * pointStep : gridLeft + plotWidth / 2;
    const int pointY = plotBottom - static_cast<int>((static_cast<uint64_t>(points[static_cast<size_t>(index)].readingTimeMs) *
                                                     static_cast<uint64_t>(plotHeight)) /
                                                    maximum);
    pointYs.push_back(pointY);
    if (index > 0) {
      const int previousX = pointCount > 1 ? gridLeft + (index - 1) * pointStep : pointX;
      drawStatisticsLine(renderer, previousX, pointYs[static_cast<size_t>(index - 1)], pointX, pointY);
    }
  }

  for (int index = 0; index < pointCount; ++index) {
    const int pointX = pointCount > 1 ? gridLeft + index * pointStep : gridLeft + plotWidth / 2;
    constexpr int pointSize = 9;
    renderer.rectangle.fill(pointX - pointSize / 2, pointYs[static_cast<size_t>(index)] - pointSize / 2, pointSize,
                            pointSize, static_cast<int>(GfxRenderer::FillTone::Gray), true);

    const std::string dateLabel = formatBookHistoryDate(points[static_cast<size_t>(index)].dateKey);
    const int labelX = std::max(0, pointX - labelLine / 2);
    const int labelBaseY = contentBottom - 2;
    renderer.text.rotated90CW(labelFont, labelX, labelBaseY, dateLabel.c_str(), true, EpdFontFamily::REGULAR);
  }
}

void Statistics::renderSingleBookBottomGrid(const int bookIdx) const {
  if (bookIdx < 0 || bookIdx >= static_cast<int>(allBooksStats.size())) {
    return;
  }
  const BookReadingStats& book = allBooksStats[static_cast<size_t>(bookIdx)];
  const SingleBookLayout layout = singleBookLayout(renderer);
  constexpr int kPanelPad = 10;
  constexpr int kCardGap = 12;
  const int panelX = 0;
  const int panelW = renderer.getScreenWidth();
  const int panelY = layout.areaY + layout.topH + layout.middleH;
  const int panelBottom = panelY + layout.bottomH;
  const int panelH = panelBottom - panelY;
  if (panelW <= 0 || panelH <= 0) return;

  // Separate the bottom statistics section from the chart above it.
  renderer.line.render(panelX, panelY, panelX + panelW - 1, panelY, true);
  renderer.line.render(panelX, panelY + 1, panelX + panelW - 1, panelY + 1, true);

  std::vector<ReadingHistoryBookEntry> entries;
  const std::vector<ReadingHistoryBookEntry>& history = getReadingHistoryBooks();
  uint32_t latestDateKey = 0;
  uint32_t maximum = 0;
  for (const ReadingHistoryBookEntry& entry : history) {
    if (entry.bookPath != book.path || entry.dateKey == 0 || entry.readingTimeMs == 0) continue;
    entries.push_back(entry);
    maximum = std::max(maximum, entry.readingTimeMs);
    if (latestDateKey == 0 || readingDateToDay(entry.dateKey) > readingDateToDay(latestDateKey)) {
      latestDateKey = entry.dateKey;
    }
  }
  if (maximum == 0) maximum = 1;

  for (int py = panelY; py < panelBottom; py += 2) {
    const int offset = ((py - panelY) & 2) != 0 ? 0 : 1;
    for (int px = panelX + offset; px < panelX + panelW; px += 2) {
      renderer.drawPixel(px, py, true);
    }
  }

  char chapters[16], pages[16], sessions[16];
  std::snprintf(chapters, sizeof(chapters), "%u", static_cast<unsigned>(book.totalChaptersRead));
  std::snprintf(pages, sizeof(pages), "%u", static_cast<unsigned>(book.totalPagesRead));
  std::snprintf(sessions, sizeof(sessions), "%u", static_cast<unsigned>(book.sessionCount));
  const char* values[] = {chapters, pages, sessions};
  const char* labels[] = {"Chapters", "Pages", "Sessions"};
  const int valueFont = fontSerifLg();
  const int labelFont = fontSansSm();
  const int valueLine = renderer.text.getLineHeight(valueFont);
  const int labelLine = renderer.text.getLineHeight(labelFont);
  const int groupH = valueLine + 4 + labelLine;
  const int cardW = (panelW - kPanelPad * 2 - kCardGap) / 2;
  const int cardH = (panelH - kPanelPad * 2 - kCardGap) / 2;
  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;

  for (int index = 0; index < 4; ++index) {
    const int row = index / 2;
    const int col = index % 2;
    const int cardX = panelX + kPanelPad + col * (cardW + kCardGap);
    const int cardY = panelY + kPanelPad + row * (cardH + kCardGap);
    BaseCarousel::renderShadow(renderer, cardX + 4, cardY + 4, cardW, cardH,
                                HomeTheme::CarouselShadowStyle::Gray);
    renderer.rectangle.fill(cardX, cardY, cardW, cardH, false, rounded,
                            SETTINGS.bitmapRoundedCorners == 2);
    renderer.rectangle.render(cardX, cardY, cardW, cardH, true, rounded,
                              SETTINGS.bitmapRoundedCorners == 2);

    if (index != 0) {
      const int statIndex = index - 1;
      const int centerX = cardX + cardW / 2;
      const int valueWidth = renderer.text.getWidth(valueFont, values[statIndex]);
      const int labelWidth = renderer.text.getWidth(labelFont, labels[statIndex]);
      const int textY = cardY + std::max(0, (cardH - groupH) / 2);
      renderer.text.render(valueFont, centerX - valueWidth / 2, textY, values[statIndex]);
      renderer.text.render(labelFont, centerX - labelWidth / 2, textY + valueLine + 4, labels[statIndex]);
      continue;
    }

    // The first card is a compact Sunday-to-Saturday bar chart. Use only
    // first-letter weekday markers to keep the card readable at this size.
    constexpr int kColumns = 7;
    constexpr int kGap = 4;
    constexpr const char* kWeekdayLabels[kColumns] = {"S", "M", "T", "W", "T", "F", "S"};
    const int chartLeft = cardX + 10;
    const int chartRight = cardX + cardW - 10;
    const int labelY = cardY + cardH - 8 - labelLine;
    const int baselineY = labelY - 6;
    const int chartTop = cardY + 10;
    const int chartHeight = std::max(1, baselineY - chartTop);
    const int barWidth = std::max(2, (chartRight - chartLeft - kGap * (kColumns - 1)) / kColumns);
    const int chartWidth = barWidth * kColumns + kGap * (kColumns - 1);
    const int chartX = cardX + (cardW - chartWidth) / 2;

    std::array<uint32_t, kColumns> dailyReading{};
    maximum = 0;
    if (latestDateKey != 0) {
      const int64_t latestDay = readingDateToDay(latestDateKey);
      const int64_t sunday = latestDay - ((mondayBasedWeekday(latestDay) + 1) % 7);
      for (int column = 0; column < kColumns; ++column) {
        const uint32_t dateKey = readingDayToDate(sunday + column);
        uint32_t readingTimeMs = 0;
        for (const ReadingHistoryBookEntry& entry : entries) {
          if (entry.dateKey == dateKey) {
            const uint32_t room = UINT32_MAX - readingTimeMs;
            readingTimeMs += std::min(room, entry.readingTimeMs);
          }
        }
        dailyReading[static_cast<size_t>(column)] = readingTimeMs;
        maximum = std::max(maximum, readingTimeMs);
      }
    }
    if (maximum == 0) maximum = 1;

    renderer.line.render(chartX, baselineY, chartX + chartWidth, baselineY, true, LineRender::Style::Dotted);
    for (int column = 0; column < kColumns; ++column) {
      const int x = chartX + column * (barWidth + kGap);
      const uint32_t readingTimeMs = dailyReading[static_cast<size_t>(column)];
      const int barHeight = readingTimeMs == 0
                                ? 0
                                : std::max(4, static_cast<int>((static_cast<uint64_t>(readingTimeMs) * chartHeight) /
                                                                 maximum));
      if (barHeight > 0) {
        const int tone = readingTimeMs * 3ULL >= static_cast<uint64_t>(maximum) * 2ULL
                             ? static_cast<int>(GfxRenderer::FillTone::Ink)
                             : static_cast<int>(GfxRenderer::FillTone::Gray);
        renderer.rectangle.fill(x, baselineY - barHeight, barWidth, barHeight, tone, true, true);
      }

      const int labelWidth = renderer.text.getWidth(labelFont, kWeekdayLabels[column]);
      renderer.text.renderGray(labelFont, x + (barWidth - labelWidth) / 2, labelY, kWeekdayLabels[column], true,
                               EpdFontFamily::REGULAR);
    }
  }
}

void Statistics::renderWeeklyChart(const int chartTop, const int chartBottom) const {
  if (monthlyChart_) {
    renderMonthlyChart(chartTop, chartBottom);
    return;
  }

  const int screenWidth = renderer.getScreenWidth();
  if (screenWidth <= 0 || chartBottom <= chartTop + 160) return;

  constexpr const char* kWeekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  const int left = 24;
  const int right = screenWidth - 24;
  const int headerFont = systemFontIdForPointSize(10);
  const int valueFont = systemFontIdForPointSize(18);
  const int smallFont = systemFontIdForPointSize(10);
  const int headerLine = renderer.text.getLineHeight(headerFont);
  const int valueLine = renderer.text.getLineHeight(valueFont);
  const int smallLine = renderer.text.getLineHeight(smallFont);
  constexpr int kChartBottomPadding = 20;
  const int chartContentBottom = chartBottom - kChartBottomPadding;
  if (chartContentBottom <= chartTop + 160) return;

  renderer.text.renderGray(headerFont, left, chartTop, "This week", true, EpdFontFamily::REGULAR);
  drawStatisticsChartPeriodButton(renderer, chartTop);
  const std::string goalText = "Goal · " + formatReadingDuration(weeklyGoalTimeMs_);
  const int goalWidth = renderer.text.getWidth(headerFont, goalText.c_str(), EpdFontFamily::REGULAR);
  renderer.text.renderGray(headerFont, right - goalWidth, chartTop, goalText.c_str(), true,
                           EpdFontFamily::REGULAR);

  const std::string totalText = formatReadingDuration(weeklyTotalReadingTimeMs_);
  const int totalY = chartTop + headerLine + 18;
  renderer.text.render(valueFont, left, totalY, totalText.c_str(), true, EpdFontFamily::BOLD);

  int percent = 0;
  if (weeklyGoalTimeMs_ > 0) {
    percent = static_cast<int>((static_cast<uint64_t>(weeklyTotalReadingTimeMs_) * 100ULL +
                                weeklyGoalTimeMs_ / 2ULL) /
                               weeklyGoalTimeMs_);
    percent = std::min(100, std::max(0, percent));
  }
  char percentText[12];
  std::snprintf(percentText, sizeof(percentText), "%d%%", percent);
  const int percentWidth = renderer.text.getWidth(valueFont, percentText, EpdFontFamily::BOLD);
  renderer.text.render(valueFont, right - percentWidth, totalY, percentText, true, EpdFontFamily::BOLD);

  const int plotTop = totalY + valueLine + 22;
  constexpr int kWeekdayToAverageGap = 52;
  const int plotBottom = chartContentBottom - smallLine - kWeekdayToAverageGap;
  const int plotHeight = plotBottom - plotTop;
  if (plotHeight <= 20) return;

  uint32_t maximum = weeklyGoalTimeMs_ > 0 ? weeklyGoalTimeMs_ / 7U : 0;
  int peakIndex = 0;
  for (size_t i = 0; i < weeklyReadingTimeMs_.size(); ++i) {
    if (weeklyReadingTimeMs_[i] > maximum) maximum = weeklyReadingTimeMs_[i];
    if (weeklyReadingTimeMs_[i] > weeklyReadingTimeMs_[static_cast<size_t>(peakIndex)]) {
      peakIndex = static_cast<int>(i);
    }
  }
  if (maximum == 0) maximum = 1;

  const int gridLeft = left + 8;
  const int gridRight = right - 8;
  const int gridWidth = std::max(1, gridRight - gridLeft);
  constexpr int barGap = 10;
  const int barWidth = std::max(8, (gridWidth - barGap * 6) / 7);

  for (int level = 0; level <= 4; ++level) {
    const int y = plotBottom - (plotHeight * level) / 4;
    renderer.line.render(gridLeft, y, gridRight, y, true, LineRender::Style::Dotted);
  }

  for (size_t i = 0; i < weeklyReadingTimeMs_.size(); ++i) {
    const int x = gridLeft + static_cast<int>(i) * (barWidth + barGap);
    const uint32_t value = weeklyReadingTimeMs_[i];
    const int barHeight = value == 0 ? 0 : std::max(4, static_cast<int>((static_cast<uint64_t>(value) * plotHeight) /
                                                                          maximum));
    if (barHeight > 0) {
      const int tone = static_cast<int>(static_cast<int>(i) == peakIndex ? GfxRenderer::FillTone::Ink
                                                                            : GfxRenderer::FillTone::Gray);
      renderer.rectangle.fill(x, plotBottom - barHeight, barWidth, barHeight, tone, true);
    }
    const int labelWidth = renderer.text.getWidth(smallFont, kWeekdays[i]);
    renderer.text.render(smallFont, x + (barWidth - labelWidth) / 2, plotBottom + 12, kWeekdays[i], true,
                         static_cast<int>(i) == peakIndex ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }

  const uint32_t averageMinutes = (weeklyTotalReadingTimeMs_ + 210000UL) / (7UL * 60000UL);
  char averageText[32];
  std::snprintf(averageText, sizeof(averageText), "Avg · %um/day", static_cast<unsigned>(averageMinutes));
  renderer.text.render(smallFont, left, chartContentBottom - smallLine, averageText, true);

  char peakText[32];
  std::snprintf(peakText, sizeof(peakText), "Peak · %s", kWeekdays[peakIndex]);
  const int peakWidth = renderer.text.getWidth(smallFont, peakText);
  renderer.text.render(smallFont, right - peakWidth, chartContentBottom - smallLine, peakText, true);
}

void Statistics::renderMonthlyChart(const int chartTop, const int chartBottom) const {
  const int screenWidth = renderer.getScreenWidth();
  if (screenWidth <= 0 || chartBottom <= chartTop + 160 || monthlyChartDays_ <= 0) return;

  const int left = 24;
  const int right = screenWidth - 24;
  const int headerFont = systemFontIdForPointSize(10);
  const int valueFont = systemFontIdForPointSize(18);
  const int smallFont = systemFontIdForPointSize(8);
  const int headerLine = renderer.text.getLineHeight(headerFont);
  const int valueLine = renderer.text.getLineHeight(valueFont);
  const int smallLine = renderer.text.getLineHeight(smallFont);
  constexpr int kChartBottomPadding = 20;
  const int chartContentBottom = chartBottom - kChartBottomPadding;
  if (chartContentBottom <= chartTop + 160) return;

  renderer.text.renderGray(headerFont, left, chartTop, "This month", true, EpdFontFamily::REGULAR);
  drawStatisticsChartPeriodButton(renderer, chartTop);
  const std::string goalText = "Goal · " + formatReadingDuration(monthlyGoalTimeMs_);
  const int goalWidth = renderer.text.getWidth(headerFont, goalText.c_str(), EpdFontFamily::REGULAR);
  renderer.text.renderGray(headerFont, right - goalWidth, chartTop, goalText.c_str(), true,
                           EpdFontFamily::REGULAR);

  const std::string totalText = formatReadingDuration(monthlyTotalReadingTimeMs_);
  const int totalY = chartTop + headerLine + 18;
  renderer.text.render(valueFont, left, totalY, totalText.c_str(), true, EpdFontFamily::BOLD);

  int percent = 0;
  if (monthlyGoalTimeMs_ > 0) {
    percent = static_cast<int>((static_cast<uint64_t>(monthlyTotalReadingTimeMs_) * 100ULL +
                                monthlyGoalTimeMs_ / 2ULL) /
                               monthlyGoalTimeMs_);
    percent = std::min(100, std::max(0, percent));
  }
  char percentText[12];
  std::snprintf(percentText, sizeof(percentText), "%d%%", percent);
  const int percentWidth = renderer.text.getWidth(valueFont, percentText, EpdFontFamily::BOLD);
  renderer.text.render(valueFont, right - percentWidth, totalY, percentText, true, EpdFontFamily::BOLD);

  const int plotTop = totalY + valueLine + 22;
  constexpr int kDateToAverageGap = 62;
  constexpr int kDateLabelOffset = 65;
  const int plotBottom = chartContentBottom - smallLine - kDateToAverageGap;
  const int plotHeight = plotBottom - plotTop;
  if (plotHeight <= 20) return;

  std::vector<int> plottedDays;
  plottedDays.reserve(static_cast<size_t>(monthlyChartDays_));
  for (int index = 0; index < monthlyChartDays_; ++index) {
    if (monthlyReadingTimeMs_[static_cast<size_t>(index)] > 0) {
      plottedDays.push_back(index);
    }
  }

  uint32_t maximum = monthlyGoalTimeMs_ > 0 ? monthlyGoalTimeMs_ / static_cast<uint32_t>(monthlyChartDays_) : 0;
  int peakDay = plottedDays.empty() ? 0 : plottedDays.front();
  for (const int day : plottedDays) {
    const uint32_t value = monthlyReadingTimeMs_[static_cast<size_t>(day)];
    if (value > maximum) maximum = value;
    if (value > monthlyReadingTimeMs_[static_cast<size_t>(peakDay)]) peakDay = day;
  }
  if (maximum == 0) maximum = 1;

  const int gridLeft = left + 8;
  const int gridRight = right - 8;
  const int gridWidth = std::max(1, gridRight - gridLeft);
  const int plottedCount = static_cast<int>(plottedDays.size());
  const int barGap = plottedCount > 20 ? 3 : 6;
  const int maxVisible = std::max(1, (gridWidth + barGap) / (kMonthlyMaxBarWidth + barGap));
  const int visibleStart = plottedCount > maxVisible
                               ? std::min(monthlyChartOffset_, plottedCount - maxVisible)
                               : 0;
  const int visibleCount = std::min(plottedCount - visibleStart, maxVisible);
  const int barWidth = visibleCount > 0
                           ? std::min(kMonthlyMaxBarWidth,
                                      std::max(4, (gridWidth - barGap * (visibleCount - 1)) / visibleCount))
                           : 0;
  const int seriesWidth = visibleCount > 0 ? visibleCount * barWidth + (visibleCount - 1) * barGap : 0;
  const int seriesLeft = gridLeft + std::max(0, (gridWidth - seriesWidth) / 2);

  for (int level = 0; level <= 4; ++level) {
    const int y = plotBottom - (plotHeight * level) / 4;
    renderer.line.render(gridLeft, y, gridRight, y, true, LineRender::Style::Dotted);
  }

  for (int plottedIndex = 0; plottedIndex < visibleCount; ++plottedIndex) {
    const int day = plottedDays[static_cast<size_t>(visibleStart + plottedIndex)];
    const int x = seriesLeft + plottedIndex * (barWidth + barGap);
    const uint32_t value = monthlyReadingTimeMs_[static_cast<size_t>(day)];
    const int barHeight = std::max(4, static_cast<int>((static_cast<uint64_t>(value) * plotHeight) / maximum));
    const int tone = static_cast<int>(day == peakDay ? GfxRenderer::FillTone::Ink : GfxRenderer::FillTone::Gray);
    renderer.rectangle.fill(x, plotBottom - barHeight, barWidth, barHeight, tone, true);

    const uint32_t dateKey = readingDayToDate(chartPeriodStartDay_ + day);
    const std::string dateLabel = formatBookHistoryDate(dateKey);
    const int labelX = x + barWidth / 2 - smallLine / 2;
    renderer.text.rotated90CW(smallFont, labelX, plotBottom + kDateLabelOffset, dateLabel.c_str(), true,
                              EpdFontFamily::REGULAR);
  }

  if (plottedDays.empty()) {
    renderer.text.renderGray(smallFont, gridLeft, plotTop + std::max(0, plotHeight - smallLine) / 2,
                             "No reading history", true, EpdFontFamily::REGULAR);
  }

  const uint32_t averageMinutes = plottedDays.empty()
                                      ? 0
                                      : (monthlyTotalReadingTimeMs_ + static_cast<uint32_t>(plottedCount) * 30000UL) /
                                            (static_cast<uint32_t>(plottedCount) * 60000UL);
  char averageText[32];
  std::snprintf(averageText, sizeof(averageText), "Avg · %um/day", static_cast<unsigned>(averageMinutes));
  renderer.text.render(smallFont, left, chartContentBottom - smallLine, averageText, true);

  if (!plottedDays.empty()) {
    char peakText[32];
    const uint32_t peakDateKey = readingDayToDate(chartPeriodStartDay_ + peakDay);
    std::snprintf(peakText, sizeof(peakText), "Peak · %s", formatBookHistoryDate(peakDateKey).c_str());
    const int peakWidth = renderer.text.getWidth(smallFont, peakText);
    renderer.text.render(smallFont, right - peakWidth, chartContentBottom - smallLine, peakText, true);
  }
}

void Statistics::renderRadialChart(const int chartTop, const int chartBottom, const int chartLeft,
                                   const int chartRight) const {
  if (chartBottom <= chartTop || chartRight <= chartLeft) return;
  const GlobalAllItemsGeom geometry = computeGlobalAllItemsGeom(renderer);
  const int rowHeight = chartBottom - chartTop;
  const int rowY = chartTop + (rowHeight - geometry.rowH) / 2;
  float ratio = 0.0f;
  if (globalStats.totalBooksStarted > 0) {
    ratio = std::min(1.0f, static_cast<float>(globalStats.totalBooksFinished) /
                              static_cast<float>(globalStats.totalBooksStarted));
  }
  drawGlobalAllItemsGaugeRow(renderer, chartLeft, chartRight, rowY, ratio, geometry, true);
}

void Statistics::content() {
  const ButtonBounds next = nextButton(renderer);
  const int contentBottom = next.y - 20;
  const int contentTopSingle = kStatsContentTop;

  if (statsTab_ == 0) {
    // The statistics implementation remains available for the Calendar view
    // and future actions, but the main Statistics page is currently a simple
    // list menu. Keep the old drawing path below without executing it.
    const int sectionTop = kWeeklyChartTop;
    const int sectionBottom = renderer.getScreenHeight();
    const int sectionHeight = std::max(0, sectionBottom - sectionTop);
    constexpr int kPanelGap = 12;
    const int usableHeight = std::max(0, sectionHeight - kPanelGap);
    const int weeklyHeight = (usableHeight * 65) / 100;
    const int split = sectionTop + weeklyHeight;
    const int radialTop = split + kPanelGap;
    const int radialHeight = std::max(0, sectionBottom - split);
    constexpr int kContentInset = 20;
    const int grayLeft = 0;
    const int grayWidth = renderer.getScreenWidth();
    if (radialHeight > 0 && grayWidth > 0) {
      // Match the carousel background: a sparse gray dither rather than a full-tone fill.
      for (int py = (split + 1) & ~1; py < sectionBottom; py += 2) {
        for (int px = (grayLeft + 1) & ~1; px < grayLeft + grayWidth; px += 2) {
          renderer.drawPixel(px, py, true);
        }
      }
      renderer.line.render(grayLeft, split, grayLeft + grayWidth, split, true);
    }
    renderWeeklyChart(sectionTop, split);
    constexpr int kRightCardGap = 16;
    char readingValue[16];
    std::snprintf(readingValue, sizeof(readingValue), "%u",
                  static_cast<unsigned>(globalStats.totalBooksStarted));
    char pagesValue[16];
    std::snprintf(pagesValue, sizeof(pagesValue), "%u", static_cast<unsigned>(globalStats.totalPagesRead));
    const int valueFont = fontSerifLg();
    const int labelFont = fontSansSm();
    const int valueWidth = renderer.text.getWidth(valueFont, readingValue);
    const int labelWidth = renderer.text.getWidth(labelFont, "Reading");
    const int pagesValueWidth = renderer.text.getWidth(valueFont, pagesValue);
    const int pagesLabelWidth = renderer.text.getWidth(labelFont, "Pages");
    // The gray panel begins at the split line; keep both the radial row and card centered in that full panel.
    const int radialRegionTop = split;
    const int radialRegionHeight = std::max(0, sectionBottom - radialRegionTop);
    constexpr int kSquareGap = 12;
    const int minimumSquareSize = std::max(valueWidth, labelWidth) + 40;
    const int availableSquareHeight = std::max(1, radialRegionHeight - 16);
    const int squareSize = std::max(minimumSquareSize,
                                    std::max(1, (availableSquareHeight - kSquareGap) / 2));
    const int cardX = kContentInset;
    const int cardTop = radialRegionTop + std::max(0, (radialRegionHeight - (squareSize * 2 + kSquareGap)) / 2);
    const int secondCardTop = cardTop + squareSize + kSquareGap;
    const int radialLeft = cardX + squareSize + kRightCardGap;
    const int radialRight = renderer.getScreenWidth() - kContentInset;
    if (squareSize > 0 && radialRight > radialLeft) {
      renderRadialChart(radialRegionTop, sectionBottom, radialLeft, radialRight);

      const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
      BaseCarousel::renderShadow(renderer, cardX + 6, cardTop + 6, squareSize, squareSize,
                                  HomeTheme::CarouselShadowStyle::Gray);
      renderer.rectangle.fill(cardX, cardTop, squareSize, squareSize, false, rounded,
                              SETTINGS.bitmapRoundedCorners == 2);
      renderer.rectangle.render(cardX, cardTop, squareSize, squareSize, true, rounded,
                                SETTINGS.bitmapRoundedCorners == 2);
      BaseCarousel::renderShadow(renderer, cardX + 6, secondCardTop + 6, squareSize, squareSize,
                                  HomeTheme::CarouselShadowStyle::Gray);
      renderer.rectangle.fill(cardX, secondCardTop, squareSize, squareSize, false, rounded,
                              SETTINGS.bitmapRoundedCorners == 2);
      renderer.rectangle.render(cardX, secondCardTop, squareSize, squareSize, true, rounded,
                                SETTINGS.bitmapRoundedCorners == 2);

      const int valueLine = renderer.text.getLineHeight(valueFont);
      const int labelLine = renderer.text.getLineHeight(labelFont);
      const int groupHeight = valueLine + 4 + labelLine;
      const int valueY = cardTop + (squareSize - groupHeight) / 2;
      const int centerX = cardX + squareSize / 2;
      renderer.text.render(valueFont, centerX - valueWidth / 2, valueY, readingValue);
      renderer.text.render(labelFont, centerX - labelWidth / 2, valueY + valueLine + 4, "Reading");

      const int pagesValueY = secondCardTop + (squareSize - groupHeight) / 2;
      const int pagesCenterX = cardX + squareSize / 2;
      renderer.text.render(valueFont, pagesCenterX - pagesValueWidth / 2, pagesValueY, pagesValue);
      renderer.text.render(labelFont, pagesCenterX - pagesLabelWidth / 2, pagesValueY + valueLine + 4, "Pages");
    }
    return;
  }

  if (statsTab_ == 1) {
    constexpr int kWeekSideMargin = 20;
    const int innerLeft = kWeekSideMargin;
    const int innerW = renderer.getScreenWidth() - (kWeekSideMargin * 2);
    renderCalendar(kCalendarContentTop, innerLeft, innerW, renderer.getScreenHeight() - 12);
    return;
  }

  if (statsTab_ == 2) {
    renderBooksView(contentTopSingle, renderer.getScreenHeight() - 12);
    return;
  }

  if (statsTab_ == 3) {
    if (selectedBookIndex_ >= 0) {
      ensureBookStatsLoaded(selectedBookIndex_);
      renderSingleBookTopSection(selectedBookIndex_);
      renderSingleBookMiddleChart(selectedBookIndex_);
      renderSingleBookBottomGrid(selectedBookIndex_);
    }
    return;
  }

  const int totalViews = 1 + static_cast<int>(allBooksStats.size());
  int v = viewIndex;
  if (v < 0) v = 0;
  if (v >= totalViews) v = totalViews - 1;

  if (v == 0) {
    if (!allBooksStats.empty()) {
      ensureBookStatsLoaded(0);
    }
    constexpr int Margin = 20;
    constexpr int kMarginX = 30;
    const int innerLeft = kMarginX;
    const int innerRight = renderer.getScreenWidth() - kMarginX;
    const int innerW = innerRight - innerLeft;

    int GAP = contentTopSingle;
    GAP = renderFirstGrid(GAP, innerLeft, innerW, Margin);
    GAP = renderGuage(GAP, innerLeft, innerRight, Margin);
    renderSecondGrid(GAP, innerLeft, innerRight, contentBottom);
  } else {
    ensureBookStatsLoaded(v - 1);
    renderSingleBookView(v - 1, contentTopSingle, contentBottom);
  }

}

void Statistics::renderBookListThumbnail(const BookListMetadata& book, const int x, const int y, const int width,
                                          const int height) const {
  ImageRender::Options imageOptions;
  imageOptions.cropToFill = true;
  imageOptions.asyncDisplayCache = true;
  imageOptions.roundedOutside = SETTINGS.bitmapRoundedCorners != 0 ? BitmapRender::RoundedOutside::PaperOutside
                                                                   : BitmapRender::RoundedOutside::None;
  bool coverDrawn = false;
  if (!book.thumbnailPath.empty()) {
    coverDrawn = ImageRender::create(renderer, book.thumbnailPath)
                     .render(x + 2, y + 2, std::max(1, width - 4), std::max(1, height - 4), imageOptions);
  }
  if (coverDrawn) {
    return;
  }

  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
  renderer.rectangle.fill(x, y, width, height, false, rounded);
  if (!rounded) {
    renderer.rectangle.dotted(x, y, width, height, true);
  }
}

void Statistics::renderBooksView(const int contentTop, const int contentBottom) {
  constexpr int rowHeight = kBooksRowHeight;
  constexpr int sideMargin = 20;
  constexpr int thumbnailWidth = 74;
  constexpr int thumbnailHeight = 104;
  constexpr int textGap = 14;
  constexpr int caretSize = 30;
  const int width = renderer.getScreenWidth();
  const int titleFont = systemFontIdForPointSize(14);
  const int descriptionFont = systemFontIdForPointSize(10);
  const int progressFont = systemFontIdForPointSize(18);
  const int caretX = width - sideMargin - caretSize;
  const int progressWidth = 74;
  const int visibleHeight = std::max(0, contentBottom - contentTop);
  const bool useLibraryIndex = !indexedBooks_.empty();
  const int bookCount = useLibraryIndex ? static_cast<int>(indexedBooks_.size())
                                       : static_cast<int>(bookListMetadata_.size());
  const int pageCount = std::max(1, (bookCount + kBooksPerPage - 1) / kBooksPerPage);
  const int pageStart = std::min(static_cast<int>(bookListMetadata_.size()), booksPage_ * kBooksPerPage);
  const int indexedPageStart = std::min(bookCount, booksPage_ * kBooksPerPage);
  const int visiblePageStart = useLibraryIndex ? indexedPageStart : pageStart;
  const int pageEnd = std::min(bookCount, visiblePageStart + kBooksPerPage);

  if (bookCount == 0) {
    renderer.text.centered(systemFontId(), contentTop + 24, "Refresh statistics to load books");
    return;
  }

  for (int index = visiblePageStart; index < pageEnd; ++index) {
    const int row = index - visiblePageStart;
    const int rowY = contentTop + row * rowHeight;
    if (rowY < contentTop || rowY + rowHeight > contentBottom) {
      continue;
    }

    BookListMetadata indexedMetadata;
    const BookListMetadata* book = nullptr;
    if (useLibraryIndex) {
      const LibraryIndex::Book& indexedBook = indexedBooks_[static_cast<size_t>(index)];
      const std::string cachePath = epubCachePathForBookPath(indexedBook.path);
      indexedMetadata.title = indexedBook.title.empty() ? "Untitled" : indexedBook.title;
      indexedMetadata.description = indexedBook.folder;
      indexedMetadata.thumbnailPath = thumbnailPathForBookCache(cachePath);
      const auto cached = std::find_if(bookListMetadata_.begin(), bookListMetadata_.end(),
                                       [&indexedMetadata](const BookListMetadata& cachedBook) {
                                         return cachedBook.title == indexedMetadata.title;
                                       });
      if (cached != bookListMetadata_.end()) {
        indexedMetadata.progressPercent = cached->progressPercent;
        if (!cached->thumbnailPath.empty()) indexedMetadata.thumbnailPath = cached->thumbnailPath;
        if (!cached->description.empty()) indexedMetadata.description = cached->description;
      }

      // The persisted list metadata can lag behind the per-book statistics
      // file. Read the same record used by the individual-book view so the
      // main list never displays a stale 0% value.
      BookReadingStats liveStats;
      if (loadBookStats(cachePath.c_str(), liveStats) && liveStats.progressPercent > 0.0f) {
        indexedMetadata.progressPercent = liveStats.progressPercent;
      }
      book = &indexedMetadata;
    } else {
      book = &bookListMetadata_[static_cast<size_t>(index)];
    }
    const int thumbY = rowY + (rowHeight - thumbnailHeight) / 2;
    renderBookListThumbnail(*book, sideMargin, thumbY, thumbnailWidth, thumbnailHeight);

    const int textX = sideMargin + thumbnailWidth + textGap;
    const int progressX = caretX - 12 - progressWidth;
    const int textWidth = std::max(40, progressX - textX - textGap);
    const int titleY = rowY + 18;
    const int titleLine = renderer.text.getLineHeight(titleFont);
    const std::string title = renderer.text.truncate(titleFont, book->title.c_str(), textWidth, EpdFontFamily::BOLD);
    renderer.text.render(titleFont, textX, titleY, title.c_str(), true, EpdFontFamily::BOLD);

    const std::string& description = book->description;
    const int descriptionY = titleY + titleLine + 12;
    const std::string descriptionLine = renderer.text.truncate(descriptionFont, description.c_str(), textWidth);
    renderer.text.render(descriptionFont, textX, descriptionY, descriptionLine.c_str(), true);

    const int progress = std::max(0, std::min(100, static_cast<int>(book->progressPercent + 0.5f)));
    const std::string progressText = std::to_string(progress) + "%";
    const int progressActualWidth = renderer.text.getWidth(progressFont, progressText.c_str());
    const int progressY = rowY + (rowHeight - renderer.text.getLineHeight(progressFont)) / 2;
    renderer.text.renderGray(progressFont, progressX + (progressWidth - progressActualWidth) / 2, progressY,
                             progressText.c_str(), true, EpdFontFamily::REGULAR);

    const int caretY = rowY + (rowHeight - caretSize) / 2;
    renderer.bitmap.icon(LibraryFilterRight, caretX, caretY, caretSize, caretSize);
    renderer.line.render(sideMargin, rowY + rowHeight - 1, width - sideMargin, rowY + rowHeight - 1, true,
                         LineRender::Style::Dotted);
  }

  if (pageCount > 1 && visibleHeight > 0) {
    constexpr int trackWidth = 2;
    constexpr int thumbWidth = 4;
    const int trackX = width - 5;
    const int thumbHeight = std::max(24, visibleHeight / pageCount);
    const int thumbTravel = std::max(1, visibleHeight - thumbHeight);
    const int thumbY = contentTop + (booksPage_ * thumbTravel) / std::max(1, pageCount - 1);
    renderer.rectangle.fill(trackX, contentTop, trackWidth, visibleHeight,
                            static_cast<int>(GfxRenderer::FillTone::Gray), true);
    renderer.rectangle.fill(trackX - 1, thumbY, thumbWidth, thumbHeight,
                            static_cast<int>(GfxRenderer::FillTone::Ink), true);
  }
}

void Statistics::loop() {
  if (indexRefreshPending_ && !LibraryIndexRefresh::isRunning()) {
    indexRefreshPending_ = false;
    loadIndexedBooks();
    updateRequired = true;
    renderPage();
    return;
  }

  if (handleControlDrawerInput()) return;

  if (statsTab_ != 0) {
    const int returnTab = statsTab_ == 3 ? 2 : 0;
    const auto closeStatsTab = [this, returnTab]() {
      statsTab_ = returnTab;
      selectedBookIndex_ = -1;
      updateRequired = true;
      renderPage();
    };
    if (SubPage::closeInput(renderer, mappedInput, closeStatsTab, statsTab_ != 2)) {
      return;
    }
    // Books uses an ordinary upward swipe for paging. Reserve the bottom edge
    // for closing so both actions remain available on the same page.
    if (statsTab_ == 2 && mappedInput.hasTouch()) {
      float swipeStartNx = 0.0f;
      float swipeStartNy = 0.0f;
      if (mappedInput.wasTouchSwipeUpInScreen(renderer, swipeStartNx, swipeStartNy) &&
          swipeStartNy >= 0.80f) {
        closeStatsTab();
        return;
      }
    } else if (statsTab_ != 2 && mappedInput.hasTouch() && mappedInput.wasTouchSwipeUpForRenderer(renderer)) {
      closeStatsTab();
      return;
    }
  } else if (closeInput()) {
    return;
  } else if (mappedInput.hasTouch() && mappedInput.wasTouchSwipeUpForRenderer(renderer)) {
    dismiss();
    return;
  }

  if (handleHamburgerTap()) return;

  if (handleRefreshTap()) return;

  if (handleChartPeriodToggleTap()) return;

  if (handleBooksInput()) return;

  const int totalViews = 1 + static_cast<int>(allBooksStats.size());

  if (statsTab_ == 1 && mappedInput.hasTouch() && mappedInput.wasTouchSwipeLeft()) {
    calendarWeekStartDay_ += 7;
    updateRequired = true;
    renderPage();
    return;
  }

  if (statsTab_ == 1 && mappedInput.hasTouch() && mappedInput.wasTouchSwipeRight()) {
    calendarWeekStartDay_ -= 7;
    updateRequired = true;
    renderPage();
    return;
  }

  if (statsTab_ == 0 && mappedInput.hasTouch() && mappedInput.wasTouchSwipeLeft()) {
    if (scrollMonthlyChart(1)) return;
    moveChartPeriod(1);
    return;
  }

  if (statsTab_ == 0 && mappedInput.hasTouch() && mappedInput.wasTouchSwipeRight()) {
    if (scrollMonthlyChart(-1)) return;
    moveChartPeriod(-1);
    return;
  }

  if (mappedInput.hasTouch()) {
    float tapX = 0.0f;
    float tapY = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) {
      renderPage();
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    loadStats();
    updateRequired = true;
    return;
  }

  if (statsTab_ != 0) {
    renderPage();
    return;
  }

  if (totalViews <= 1) {
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    viewIndex = (viewIndex + totalViews - 1) % totalViews;
    updateRequired = true;
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    viewIndex = (viewIndex + 1) % totalViews;
    updateRequired = true;
    return;
  }

  renderPage();
}

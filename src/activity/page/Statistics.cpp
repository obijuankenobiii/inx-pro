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
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <esp_task_wdt.h>

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
#include "images/Refresh.h"
#include "images/StatsPie.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"

extern void onGoToHome();

Statistics::Statistics(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> close)
    : SubPage("Statistics", renderer, mappedInput, std::move(close)), viewIndex(0) {}

namespace {

constexpr int FONT_SANS_SM = MONTSERRAT_8_FONT_ID;
constexpr int FONT_SERIF = MONTSERRAT_14_FONT_ID;
constexpr int FONT_SERIF_MD = MONTSERRAT_16_FONT_ID;
constexpr int FONT_SERIF_LG = MONTSERRAT_18_FONT_ID;
constexpr int FONT_SERIF_SM = MONTSERRAT_12_FONT_ID;
constexpr float kPi = 3.14159265f;

constexpr int kCalendarGap = 5;

constexpr int kStatsContentTop = navigation::Menu::height + 20;
constexpr int kCalendarContentTop = navigation::Menu::height + 10;
constexpr int kWeeklyChartTop = navigation::Menu::height + 12;

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

ButtonBounds nextButton(const GfxRenderer& renderer) {
  return {renderer.getScreenWidth() - 80, renderer.getScreenHeight() - 20 - Button::height, 60, Button::height};
}

struct StatisticsAction {
  const uint8_t* icon;
  const char* label;
};

constexpr StatisticsAction kStatisticsActions[] = {
    {Calendar, "Calendar"}, {StatsPie, "Stats"}, {DeviceDisplay, "Display"}, {Refresh, "Refresh"}};

static std::string epubCachePathForBookPath(const std::string& bookPath) {
  return "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(bookPath));
}

static bool statsFileExistsForCachePath(const std::string& cachePath) {
  if (cachePath.empty()) {
    return false;
  }
  const std::string statsPath = cachePath + "/statistics.bin";
  return SdMan.exists(statsPath.c_str());
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

  const int tw = renderer.text.getWidth(FONT_SERIF_MD, centerPct);
  const int lhMd = renderer.text.getLineHeight(FONT_SERIF_MD);
  renderer.text.render(FONT_SERIF_MD, cx - tw / 2, cy - lhMd / 2, centerPct);
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
  const int lhSm = renderer.text.getLineHeight(FONT_SANS_SM);
  const int lhNum = renderer.text.getLineHeight(FONT_SERIF_LG);
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
  const int twFin = renderer.text.getWidth(FONT_SERIF_LG, buf);
  const int yNum = yRule + g.kMetricsPadT;
  renderer.text.render(FONT_SERIF_LG, leftCx - twFin / 2, yNum, buf);
  const char* labFin = "Books finished";
  const int twLabF = renderer.text.getWidth(FONT_SANS_SM, labFin);
  const int yLabFin = yNum + g.lhNum + 4;
  renderer.text.render(FONT_SANS_SM, leftCx - twLabF / 2, yLabFin, labFin);

  snprintf(buf, sizeof(buf), "%u", booksOpened);
  const int rightCx = midX + (innerRight - midX) / 2;
  const int twH = renderer.text.getWidth(FONT_SERIF_LG, buf);
  renderer.text.render(FONT_SERIF_LG, rightCx - twH / 2, yNum, buf);
  const char* labH = "Books opened";
  const int twLabH = renderer.text.getWidth(FONT_SANS_SM, labH);
  renderer.text.render(FONT_SANS_SM, rightCx - twLabH / 2, yLabFin, labH);

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
  const int lhVal = renderer.text.getLineHeight(FONT_SERIF_LG);
  const int lhLab = renderer.text.getLineHeight(FONT_SANS_SM);
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
    const std::string valT = renderer.text.truncate(FONT_SERIF_LG, val, cw);
    const std::string labT = renderer.text.truncate(FONT_SANS_SM, lab, cw);
    renderer.text.render(FONT_SERIF_LG, cellLeft, rowTop, valT.c_str());
    renderer.text.render(FONT_SANS_SM, cellLeft, rowTop + lhVal + kValLabGap, labT.c_str());
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

void Statistics::loadStats() {
  bookIndexPending_ = false;
  ScreenComponents::LoadingProgressLayout layout =
      ScreenComponents::LoadingProgress::show(renderer, "Loading statistics...", 12);
  allBooksStats = getAllBooksStats();
  loadedBookStatsFlags_.assign(allBooksStats.size(), 1);
  ScreenComponents::LoadingProgress::setProgress(renderer, layout, 40);
  std::sort(allBooksStats.begin(), allBooksStats.end(),
            [](const BookReadingStats& a, const BookReadingStats& b) { return a.lastReadTimeMs > b.lastReadTimeMs; });
  ScreenComponents::LoadingProgress::setProgress(renderer, layout, 65);
  globalStats = aggregateGlobalStatsFromBooks(allBooksStats);
  ScreenComponents::LoadingProgress::setProgress(renderer, layout, 90);
  viewIndex = 0;
  ScreenComponents::LoadingProgress::setProgress(renderer, layout, 100);
  saveGlobalStats(globalStats);
  refreshWeeklyChartData();
}

void Statistics::refreshWeeklyChartData() {
  weeklyReadingTimeMs_.fill(0);
  weeklyTotalReadingTimeMs_ = 0;
  weeklyGoalTimeMs_ = 0;
  weeklyChartWeekStartDay_ = 0;

  uint32_t today = 0;
  const std::vector<ReadingHistoryEntry>& history = getReadingHistory();
  if (currentRtcDateKey(today)) {
    weeklyChartWeekStartDay_ = readingDateToDay(today);
    weeklyChartWeekStartDay_ -= mondayBasedWeekday(weeklyChartWeekStartDay_);
  } else if (!history.empty()) {
    weeklyChartWeekStartDay_ = readingDateToDay(history.back().dateKey);
    weeklyChartWeekStartDay_ -= mondayBasedWeekday(weeklyChartWeekStartDay_);
  } else {
    return;
  }

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

void Statistics::hydrateFromStorage() {
  if (!loadGlobalStats(globalStats)) {
    globalStats = GlobalReadingStats();
  }
  allBooksStats.clear();
  loadedBookStatsFlags_.clear();
  // The recent list is small and gives the first screen enough book data to
  // render immediately. The complete cache directory scan is deferred until
  // after that first frame is displayed.
  indexBookStatsPaths(false);
  bookIndexPending_ = true;
}

void Statistics::finishDeferredBookIndex() {
  if (!bookIndexPending_) {
    return;
  }
  bookIndexPending_ = false;
  allBooksStats.clear();
  loadedBookStatsFlags_.clear();
  indexBookStatsPaths(true);
  updateRequired = true;
}

void Statistics::indexBookStatsPaths(const bool includeMetadata) {
  std::set<std::string> seen;
  uint32_t scanned = 0;
  const auto service = [&] {
    if ((++scanned & 7u) == 0u) {
      esp_task_wdt_reset();
      yield();
    }
  };
  loadedBookStatsFlags_.clear();
  auto addCachePath = [&](const std::string& cachePath, const RecentBook* recent) {
    if (cachePath.empty() || seen.count(cachePath) != 0 || !statsFileExistsForCachePath(cachePath)) {
      return;
    }
    BookReadingStats placeholder;
    placeholder.path = cachePath;
    if (recent != nullptr) {
      placeholder.title = recent->title;
      placeholder.author = recent->author;
      if (recent->progress >= 0.f) {
        placeholder.progressPercent = recent->progress * 100.f;
      }
    }
    allBooksStats.push_back(placeholder);
    loadedBookStatsFlags_.push_back(0);
    seen.insert(cachePath);
  };

  RECENT_BOOKS.loadFromFile();
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    service();
    std::string cachePath = book.cachePath;
    if (cachePath.empty() && !book.path.empty()) {
      cachePath = epubCachePathForBookPath(book.path);
    }
    addCachePath(cachePath, &book);
  }

  auto appendMetadataRoot = [&](const char* rootDir) {
    FsFile root = SdMan.open(rootDir);
    if (!root || !root.isDirectory()) {
      if (root) {
        root.close();
      }
      return;
    }

    char name[128];
    root.rewindDirectory();
    while (true) {
      FsFile entry = root.openNextFile();
      if (!entry) {
        break;
      }
      if (entry.isDirectory()) {
        entry.getName(name, sizeof(name));
        addCachePath(std::string(rootDir) + "/" + std::string(name), nullptr);
      }
      entry.close();
      service();
    }
    root.close();
  };

  if (includeMetadata) {
    appendMetadataRoot("/.metadata/epub");
    appendMetadataRoot("/.metadata/xtc");
  }
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
    int lineHeight = renderer.text.getLineHeight(FONT_SERIF_SM);

    std::string remaining = title;
    int lineCount = 0;

    while (!remaining.empty() && lineCount < 3) {
      std::string line;
      int lineWidth = 0;

      while (!remaining.empty()) {
        size_t spacePos = remaining.find(' ');
        std::string word = (spacePos != std::string::npos) ? remaining.substr(0, spacePos) : remaining;

        int wordWidth = renderer.text.getWidth(FONT_SERIF_SM, word.c_str(), EpdFontFamily::REGULAR);

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

      int textWidth = renderer.text.getWidth(FONT_SERIF_SM, line.c_str(), EpdFontFamily::REGULAR);
      int textX = x + (width - textWidth) / 2;
      renderer.text.render(FONT_SERIF_SM, textX, lineY, line.c_str(), true, EpdFontFamily::REGULAR);
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
  Page::onEnter();

  hydrateFromStorage();
  viewIndex = 0;
  statsTab_ = 0;
  controlDrawerOpen_ = false;

  uint32_t calendarDate = 0;
  currentRtcDateKey(calendarDate);
  if (calendarDate == 0) {
    const std::vector<ReadingHistoryEntry>& history = getReadingHistory();
    if (!history.empty()) calendarDate = history.back().dateKey;
  }
  calendarWeekStartDay_ = calendarDate != 0
                              ? readingDateToDay(calendarDate) - mondayBasedWeekday(readingDateToDay(calendarDate))
                              : 0;
  refreshWeeklyChartData();
  SETTINGS.runHalfRefreshOnLoadIfEnabled(renderer, SystemSetting::RefreshOnLoadPage::Statistics);
}

void Statistics::onExit() {
  Page::onExit();

  std::vector<BookReadingStats>().swap(allBooksStats);
  std::vector<uint8_t>().swap(loadedBookStatsFlags_);
  bookIndexPending_ = false;
}

void Statistics::menu() {
  if (statsTab_ == 1) {
    SubPage::header(renderer, "Week");
    return;
  }
  title();
  renderer.bitmap.icon(Close, renderer.getScreenWidth() - 60, navigation::Menu::topPadding, 40, 40);
  if (controlDrawerOpen_) drawControlDrawer();
}

void Statistics::title() const {
  renderer.bitmap.icon(Hamburger, navigation::Menu::leftMargin, navigation::Menu::topPadding,
                       navigation::Menu::iconSize, navigation::Menu::iconSize);
  const int font = MONTSERRAT_16_FONT_ID;
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
  const int hamburgerX = navigation::Menu::leftMargin - 10;
  const int hamburgerY = navigation::Menu::topPadding - 10;
  const int hamburgerSize = navigation::Menu::iconSize;
  if (tapX < hamburgerX || tapX >= hamburgerX + hamburgerSize || tapY < hamburgerY ||
      tapY >= hamburgerY + hamburgerSize) {
    mappedInput.restoreTouchTapInScreen(renderer, tapNx, tapNy);
    return false;
  }
  controlDrawerOpen_ = true;
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
  } else if (item == 3) {
    loadStats();
  }
  updateRequired = true;
  renderPage();
  return true;
}

int Statistics::renderRecent(int y, int innerLeft, int innerRight, int innerW, int Margin) const {
  constexpr int kThumbTextGap = 15;
  constexpr int kGlobalThumbOuterPad = 2;
  constexpr int g8 = 8;
  constexpr int g10 = 10;

  const int lhSerif = renderer.text.getLineHeight(FONT_SERIF);
  const int lhSans = renderer.text.getLineHeight(systemFontId());
  const int lhSm = renderer.text.getLineHeight(FONT_SANS_SM);
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
    std::string titleLine = renderer.text.truncate(FONT_SERIF, cur.title.c_str(), textColW, EpdFontFamily::ITALIC);
    renderer.text.render(FONT_SERIF, textX, yTitle, titleLine.c_str(), true, EpdFontFamily::ITALIC);
    const int yAuthor = yTitle + lhSerif + g8;
    if (!cur.author.empty()) {
      std::string auth = renderer.text.truncate(systemFontId(), cur.author.c_str(), textColW);
      renderer.text.render(systemFontId(), textX, yAuthor, auth.c_str());
    }
    const int yProg = yAuthor + lhSans + g10;
    char progLabel[48];
    snprintf(progLabel, sizeof(progLabel), "Book progress: %.0f%%", prog * 100.f);
    renderer.text.render(FONT_SANS_SM, textX, yProg, progLabel);
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

  const int smallFont = FONT_SANS_SM;
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
  const int totalFont = FONT_SERIF_SM;
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

  const int lhSerif = renderer.text.getLineHeight(FONT_SERIF);
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
  std::string titleLine = renderer.text.truncate(FONT_SERIF, b.title.c_str(), textMaxW, EpdFontFamily::ITALIC);
  const int yTitle = yCoverTop + rowHeight + gapCoverTitle;
  renderer.text.render(FONT_SERIF, innerLeft, yTitle, titleLine.c_str(), true, EpdFontFamily::ITALIC);
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
  renderer.text.centered(FONT_SANS_SM, contentBottom - 5, footer);
}

void Statistics::renderWeeklyChart(const int chartTop, const int chartBottom) const {
  const int screenWidth = renderer.getScreenWidth();
  if (screenWidth <= 0 || chartBottom <= chartTop + 160) return;

  constexpr const char* kWeekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  const int left = 24;
  const int right = screenWidth - 24;
  const int headerFont = MONTSERRAT_10_FONT_ID;
  const int valueFont = MONTSERRAT_18_FONT_ID;
  const int smallFont = MONTSERRAT_10_FONT_ID;
  const int headerLine = renderer.text.getLineHeight(headerFont);
  const int valueLine = renderer.text.getLineHeight(valueFont);
  const int smallLine = renderer.text.getLineHeight(smallFont);
  constexpr int kChartBottomPadding = 20;
  const int chartContentBottom = chartBottom - kChartBottomPadding;
  if (chartContentBottom <= chartTop + 160) return;

  renderer.text.renderGray(headerFont, left, chartTop, "This week", true, EpdFontFamily::REGULAR);
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
    const int valueFont = FONT_SERIF_LG;
    const int labelFont = FONT_SANS_SM;
    const int valueWidth = renderer.text.getWidth(valueFont, readingValue);
    const int labelWidth = renderer.text.getWidth(labelFont, "Reading");
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

void Statistics::loop() {
  if (statsTab_ == 1 && mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    statsTab_ = 0;
    viewIndex = 0;
    updateRequired = true;
    renderPage();
    return;
  }
  if (handleControlDrawerInput()) return;
  if (closeInput()) return;

  if (handleHamburgerTap()) return;

  // Let the cached global view reach the display before scanning every
  // metadata cache directory. This keeps entering Statistics responsive while
  // preserving the full per-book list for navigation.
  if (!updateRequired && bookIndexPending_) {
    finishDeferredBookIndex();
    return;
  }

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

  if (statsTab_ == 0 && totalViews > 1 && mappedInput.hasTouch() && mappedInput.wasTouchSwipeRight()) {
    viewIndex = (viewIndex + 1) % totalViews;
    updateRequired = true;
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

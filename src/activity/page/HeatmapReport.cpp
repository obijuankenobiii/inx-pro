#include "HeatmapReport.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "activity/page/components/global/Button.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiLayout.h"

namespace {

constexpr int kHeatmapTop = 68;
constexpr int kHeatmapHeight = 350;
constexpr int kReportPadding = 20;
constexpr int kSmallFont = MONTSERRAT_8_FONT_ID;
constexpr int kBodyFont = MONTSERRAT_10_FONT_ID;

std::string titleForPath(const std::string& path, const std::vector<BookReadingStats>& stats) {
  for (const BookReadingStats& book : stats) {
    if (book.path == path && !book.title.empty()) return book.title;
  }
  return path.empty() ? "Book" : path;
}

}  // namespace

HeatmapReport::HeatmapReport(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const HomeTheme::HeatmapView view, std::function<void()> close)
    : SubPage("Reading Heatmap", renderer, mappedInput, std::move(close)), view_(view), heatmap_(renderer) {}

void HeatmapReport::onEnter() {
  SubPage::onEnter();
  bookHistory_ = getReadingHistoryBooks();
  bookStats_ = getAllBooksStats();
  currentRtcDateKey(selectedDateKey_);
  if (selectedDateKey_ == 0 && !bookHistory_.empty()) selectedDateKey_ = bookHistory_.back().dateKey;
}

std::string HeatmapReport::dateLabel(const uint32_t dateKey) const {
  if (dateKey == 0) return "Select a day";
  char value[24];
  snprintf(value, sizeof(value), "%04lu-%02lu-%02lu", static_cast<unsigned long>(dateKey / 10000UL),
           static_cast<unsigned long>((dateKey / 100UL) % 100UL), static_cast<unsigned long>(dateKey % 100UL));
  return value;
}

void HeatmapReport::renderBookTotals(const int y, const uint32_t dateKey) const {
  std::vector<BookTotal> totals;
  for (const ReadingHistoryBookEntry& entry : bookHistory_) {
    if (entry.dateKey != dateKey || entry.bookPath.empty()) continue;
    auto it = std::find_if(totals.begin(), totals.end(),
                           [&entry](const BookTotal& value) { return value.title == entry.bookPath; });
    if (it == totals.end()) {
      totals.push_back({entry.bookPath, entry.readingTimeMs});
    } else {
      it->readingTimeMs += entry.readingTimeMs;
    }
  }

  if (totals.empty()) {
    renderer.text.render(kBodyFont, kReportPadding, y, dateKey == 0 ? "Tap a heatmap day" : "No books recorded", true,
                         EpdFontFamily::REGULAR);
    return;
  }

  const int itemFont = systemFontId();
  const int lineHeight = renderer.text.getLineHeight(itemFont);
  const int rowHeight = UiLayout::LIST_ITEM_HEIGHT;
  const int right = renderer.getScreenWidth() - kReportPadding;
  const int maxRows = std::max(1, (renderer.getScreenHeight() - y) / rowHeight);
  for (int i = 0; i < static_cast<int>(totals.size()) && i < maxRows; ++i) {
    const BookTotal& total = totals[static_cast<size_t>(i)];
    const std::string title = titleForPath(total.title, bookStats_);
    const float hours = static_cast<float>(total.readingTimeMs) / 3600000.0f;
    char hoursText[24];
    snprintf(hoursText, sizeof(hoursText), "%.1fh", hours);
    const int hoursWidth = renderer.text.getWidth(kBodyFont, hoursText);
    const int titleWidth = std::max(1, right - kReportPadding - hoursWidth - 16);
    const std::string shown = renderer.text.truncate(itemFont, title.c_str(), titleWidth);
    const int rowY = y + i * rowHeight;
    const int textY = rowY + (rowHeight - lineHeight) / 2;
    renderer.text.render(itemFont, kReportPadding, textY, shown.c_str(), true, EpdFontFamily::REGULAR);
    renderer.text.render(itemFont, right - hoursWidth, textY, hoursText, true, EpdFontFamily::REGULAR);
    if (i + 1 < static_cast<int>(totals.size()) && i + 1 < maxRows) {
      renderer.line.render(0, rowY + rowHeight - 1, renderer.getScreenWidth(), rowY + rowHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }
}

void HeatmapReport::content() {
  heatmap_.render(0, kHeatmapTop, renderer.getScreenWidth(), kHeatmapHeight, view_, false,
                  HomeTheme::CarouselLabelColor::Black);

  const int headingY = kHeatmapTop + kHeatmapHeight + 8;
  renderer.text.render(kBodyFont, kReportPadding, headingY, "Books read", true, EpdFontFamily::BOLD);
  const std::string selectedLabel = dateLabel(selectedDateKey_);
  const int dateWidth = renderer.text.getWidth(kSmallFont, selectedLabel.c_str());
  renderer.text.render(kSmallFont, renderer.getScreenWidth() - kReportPadding - dateWidth, headingY + 2,
                       selectedLabel.c_str(), true, EpdFontFamily::REGULAR);
  renderBookTotals(headingY + renderer.text.getLineHeight(kBodyFont) + 10, selectedDateKey_);
}

void HeatmapReport::loop() {
  if (closeInput()) return;
  if (mappedInput.hasTouch()) {
    float tapX = 0.0f;
    float tapY = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) {
      const int x = static_cast<int>(tapX * renderer.getScreenWidth());
      const int y = static_cast<int>(tapY * renderer.getScreenHeight());
      const uint32_t dateKey = heatmap_.hitTestDateKey(x, y, 0, kHeatmapTop, renderer.getScreenWidth(),
                                                        kHeatmapHeight, view_, false);
      if (dateKey != 0) {
        selectedDateKey_ = dateKey;
        updateRequired = true;
      }
      renderPage();
      return;
    }
  }
  renderPage();
}

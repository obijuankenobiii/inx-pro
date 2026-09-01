#pragma once

#include "SubPage.h"
#include "components/widget/Heatmap.h"

#include <cstdint>
#include <string>
#include <vector>

#include "state/HomeTheme.h"
#include "state/Statistics.h"

/** Detailed heatmap report opened from a long-pressed home heatmap widget. */
class HeatmapReport final : public SubPage {
 public:
  HeatmapReport(GfxRenderer& renderer, MappedInputManager& mappedInput, HomeTheme::HeatmapView view,
                std::function<void()> close);

  const char* name() const override { return "Reading Heatmap"; }

  void onEnter() override;
  void loop() override;

 protected:
  void content() override;

 private:
  struct BookTotal {
    std::string title;
    uint32_t readingTimeMs = 0;
  };

  void renderBookTotals(int y, uint32_t dateKey) const;
  std::string dateLabel(uint32_t dateKey) const;

  HomeTheme::HeatmapView view_;
  Heatmap heatmap_;
  uint32_t selectedDateKey_ = 0;
  std::vector<ReadingHistoryBookEntry> bookHistory_;
  std::vector<BookReadingStats> bookStats_;
};

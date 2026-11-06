#pragma once

class GfxRenderer;

/** Home widget showing the current week's reading time as a bar chart. */
class WeeklyStatistics final {
 public:
  explicit WeeklyStatistics(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height, bool compact = false, bool lineChart = false,
              bool showSummary = true) const;
  void preview(int x, int y, int width, int height, bool compact = false, bool lineChart = false,
               bool showSummary = true) const;

 private:
  void renderContent(int x, int y, int width, int height, bool sample, bool compact, bool lineChart,
                     bool showSummary) const;

  GfxRenderer& renderer_;
};

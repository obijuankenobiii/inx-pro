#pragma once

#include "state/HomeTheme.h"

class GfxRenderer;

class Heatmap final {
 public:
  explicit Heatmap(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height, HomeTheme::HeatmapView view, bool showLabel = true,
              HomeTheme::CarouselLabelColor labelColor = HomeTheme::CarouselLabelColor::Black) const;
  void preview(int x, int y, int width, int height, HomeTheme::HeatmapView view, bool showLabel = true,
               HomeTheme::CarouselLabelColor labelColor = HomeTheme::CarouselLabelColor::Black) const;
  bool needsRefresh(HomeTheme::HeatmapView view) const;

 private:
  void renderContent(int x, int y, int width, int height, HomeTheme::HeatmapView view, bool sample, bool showLabel,
                     HomeTheme::CarouselLabelColor labelColor) const;

  GfxRenderer& renderer_;
  mutable uint64_t renderedKey_ = 0;
};

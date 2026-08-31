#pragma once

#include "state/HomeTheme.h"

class GfxRenderer;

class Heatmap final {
 public:
  explicit Heatmap(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height, HomeTheme::HeatmapView view) const;
  void preview(int x, int y, int width, int height, HomeTheme::HeatmapView view) const;
  bool needsRefresh(HomeTheme::HeatmapView view) const;

 private:
  void renderContent(int x, int y, int width, int height, HomeTheme::HeatmapView view, bool sample) const;

  GfxRenderer& renderer_;
  mutable uint64_t renderedKey_ = 0;
};

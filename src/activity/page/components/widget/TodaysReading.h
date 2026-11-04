#pragma once

#include <cstdint>

class GfxRenderer;

class TodaysReading final {
 public:
  explicit TodaysReading(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height, bool compact = false) const;
  void preview(int x, int y, int width, int height, bool compact = false) const;
  bool buttonHitTest(int tapX, int tapY, int areaX, int areaY, int areaWidth, int areaHeight,
                     bool compact = false) const;
  bool needsRefresh() const;

 private:
  void renderContent(int x, int y, int width, int height, bool sample, bool compact) const;

  GfxRenderer& renderer_;
  mutable uint64_t renderedKey_ = 0;
};

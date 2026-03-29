#pragma once

#include <cstdint>

class GfxRenderer;

class TodaysReading final {
 public:
  explicit TodaysReading(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height) const;
  void preview(int x, int y, int width, int height) const;
  bool buttonHitTest(int tapX, int tapY, int areaX, int areaY, int areaWidth, int areaHeight) const;
  bool needsRefresh() const;

 private:
  void renderContent(int x, int y, int width, int height, bool sample) const;

  GfxRenderer& renderer_;
  mutable uint64_t renderedKey_ = 0;
};

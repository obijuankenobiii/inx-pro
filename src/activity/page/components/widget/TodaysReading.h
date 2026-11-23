#pragma once

#include <cstdint>

class GfxRenderer;

class TodaysReading final {
 public:
  explicit TodaysReading(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height, bool compact = false, bool compact20 = false,
              bool compact30 = false) const;
  void preview(int x, int y, int width, int height, bool compact = false, bool compact20 = false,
               bool compact30 = false) const;
  bool buttonHitTest(int tapX, int tapY, int areaX, int areaY, int areaWidth, int areaHeight,
                     bool compact = false, bool compact20 = false) const;
  bool needsRefresh() const;

 private:
  void renderContent(int x, int y, int width, int height, bool sample, bool compact, bool compact20,
                     bool compact30) const;

  GfxRenderer& renderer_;
  mutable uint64_t renderedKey_ = 0;
};

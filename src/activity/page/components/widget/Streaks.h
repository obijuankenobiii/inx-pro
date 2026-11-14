#pragma once

#include <cstdint>

class GfxRenderer;

/** Home widget showing the consecutive reading streak and the current week. */
class Streaks final {
 public:
  explicit Streaks(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height, bool monthly = false, bool compact = false,
              bool compact20 = false) const;
  void preview(int x, int y, int width, int height, bool monthly = false, bool compact = false,
               bool compact20 = false) const;
  bool needsRefresh() const;

 private:
  void renderContent(int x, int y, int width, int height, bool sample, bool monthly, bool compact,
                     bool compact20) const;
  uint64_t dataKey() const;

  GfxRenderer& renderer_;
  mutable uint64_t renderedKey_ = 0;
};

#pragma once

#include <cstdint>

class GfxRenderer;

class Clock final {
 public:
  explicit Clock(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height) const;
  bool needsRefresh() const;

 private:
  bool readDateTime(uint64_t& key) const;

  GfxRenderer& renderer_;
  mutable uint64_t renderedKey_ = 0;
};

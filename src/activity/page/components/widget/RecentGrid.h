#pragma once

#include <string>

class GfxRenderer;

class RecentGrid final {
 public:
  static constexpr int kPageSize = 6;

  explicit RecentGrid(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height, int pageIndex = 0) const;
  void preview(int x, int y, int width, int height) const;
  int hitTest(int x, int y, int width, int height, int pageIndex, int tapX, int tapY) const;

 private:
  std::string featuredDescription(const struct RecentBook& book) const;

  GfxRenderer& renderer_;
  mutable std::string featuredDescriptionPath_;
  mutable std::string featuredDescription_;
};

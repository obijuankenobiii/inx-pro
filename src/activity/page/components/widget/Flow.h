#pragma once

#include <string>

class GfxRenderer;

/** A cover flow showing recent book covers with title and author details. */
class Flow final {
 public:
  explicit Flow(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height, int selectedIndex = 0,
              bool limitCardsToAvailableBooks = false) const;
  void preview(int x, int y, int width, int height, bool oneByOne = false) const;
  bool hitTest(int x, int y, int boundsX, int boundsY, int boundsWidth, int boundsHeight) const;

 private:
  int ratingStars(const struct RecentBook& book) const;
  const std::string& descriptionFor(const struct RecentBook& book) const;

  GfxRenderer& renderer_;
  mutable std::string ratingCachePath_;
  mutable int cachedRatingStars_ = -1;
  mutable std::string descriptionCachePath_;
  mutable std::string cachedDescription_;
};

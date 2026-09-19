#pragma once

#include <string>

#include "BaseCarousel.h"

struct RecentBook;

class Recent final : public BaseCarousel {
 public:
  explicit Recent(GfxRenderer& renderer) : BaseCarousel(renderer) {}

  void render(int x, int y, int width, int height, bool background = true,
              HomeTheme::CarouselStyle style = HomeTheme::CarouselStyle::Left, bool showLabel = true,
              HomeTheme::CarouselLabelColor labelColor = HomeTheme::CarouselLabelColor::Black,
              HomeTheme::CarouselShadowStyle shadowStyle = HomeTheme::CarouselShadowStyle::None,
              bool showTitle = true, bool showAuthor = true, bool showProgress = true, bool showRating = true) const;
  void preview(int x, int y, int width, int height, bool background = true,
               HomeTheme::CarouselStyle style = HomeTheme::CarouselStyle::Left, bool showLabel = true,
               HomeTheme::CarouselLabelColor labelColor = HomeTheme::CarouselLabelColor::Black,
               HomeTheme::CarouselShadowStyle shadowStyle = HomeTheme::CarouselShadowStyle::None,
               bool showTitle = true, bool showAuthor = true, bool showProgress = true, bool showRating = true) const;
  int hitTest(int x, int y, int areaX, int areaY, int areaW, int areaH) const;

 private:
  int ratingStars(const RecentBook& book) const;

  mutable std::string ratingCachePath_;
  mutable int cachedRatingStars_ = -1;
};

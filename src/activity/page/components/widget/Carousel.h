#pragma once

#include "BaseCarousel.h"

struct RecentBook;

class Carousel final : public BaseCarousel {
 public:
  static constexpr int kHeight = 340;

  explicit Carousel(GfxRenderer& renderer) : BaseCarousel(renderer) {}

  void render(int index, int x, int y, int width, int height, bool background = true,
              HomeTheme::CarouselStyle style = HomeTheme::CarouselStyle::Centered, bool showLabel = true,
              HomeTheme::CarouselLabelColor labelColor = HomeTheme::CarouselLabelColor::Black,
              HomeTheme::CarouselShadowStyle shadowStyle = HomeTheme::CarouselShadowStyle::None) const;
  /** Promote all capped recent-thumbnail display caches into PSRAM without drawing. */
  void preload(int index, int x, int y, int width, int height,
               HomeTheme::CarouselStyle style = HomeTheme::CarouselStyle::Centered, bool showLabel = true,
               HomeTheme::CarouselLabelColor labelColor = HomeTheme::CarouselLabelColor::Black) const;
  void preview(int x, int y, int width, int height, bool background = true,
               HomeTheme::CarouselStyle style = HomeTheme::CarouselStyle::Centered, bool showLabel = true,
               HomeTheme::CarouselLabelColor labelColor = HomeTheme::CarouselLabelColor::Black,
               HomeTheme::CarouselShadowStyle shadowStyle = HomeTheme::CarouselShadowStyle::None) const;
  int hitTest(int index, int count, int x, int y, int areaX, int areaY, int areaW, int areaH,
              HomeTheme::CarouselStyle style = HomeTheme::CarouselStyle::Centered, bool showLabel = true,
              HomeTheme::CarouselLabelColor labelColor = HomeTheme::CarouselLabelColor::Black) const;

 private:
  void renderLeft(int index, int x, int y, int width, int height, HomeTheme::CarouselShadowStyle shadowStyle) const;
  void previewLeft(int x, int y, int width, int height, HomeTheme::CarouselShadowStyle shadowStyle) const;
  int hitTestLeft(int index, int count, int x, int y, int areaX, int areaY, int areaW, int areaH) const;
};
